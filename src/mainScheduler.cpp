// the task parallel .
// mainParallel splits ONE algorithm across processes. this one runs FOUR DIFFERENT
// algorithms at the same time, which is what makes it task parallelism.
//
// this is the master and worker scheduler. it runs the four DIFFERENT algorithms
// at the same time on different processes:
//   process 0 (the master) runs task A (the spread) day after day, and does NOT
//     wait for the analyses. while it is already computing day t+1, the workers
//     are still analysing day t. that overlap is the pipeline.
//   the other processes (workers) each take an analysis job the master hands them,
//     run it (task B, C or D), and send the answer back.
//
// every task records when it starts and finishes, and the master writes all of
// that to results/timeline.csv. plotting that file shows the four algorithms
// overlapping in time, which is the proof that this is real task parallelism.
//
// usage:  mpiexec -n <processes> ./taskScheduler [numberOfNodes] [networkType] [seed]
//   use at least 2 processes; 4 (a master plus three workers) is ideal.
#include "graph.h"
#include "networkGenerators.h"
#include "seirModel.h"
#include "analysisTasks.h"

#include <mpi.h>
#include <omp.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <deque>
#include <filesystem>

// what kind of job a message carries
static const int TASK_A = 0;   // spread (the master does this itself)
static const int TASK_B = 1;   // contact tracing
static const int TASK_C = 2;   // intervention targeting
static const int TASK_D = 3;   // outbreak clustering
static const int STOP   = -1;  // tells a worker to finish

static const int TAG_JOB = 10;
static const int TAG_STATE = 11;
static const int TAG_RESULT = 12;
static const int TAG_TARGETS = 13;   // the list of people task C wants vaccinated

// job states from the proposal: WAITING, READY, IN-FLIGHT, COMPLETED.
// the extra arrow is the fault handling, a worker that misses its deadline loses
// the job back to READY.
enum JobStatus { STATUS_WAITING, STATUS_READY, STATUS_IN_FLIGHT, STATUS_COMPLETED };

static const char* nameOfStatus(int status) {
    if (status == STATUS_WAITING)   return "WAITING";
    if (status == STATUS_READY)     return "READY";
    if (status == STATUS_IN_FLIGHT) return "IN-FLIGHT";
    return "COMPLETED";
}

// what a worker sends back after finishing a job
struct JobResult {
    int taskId;      // which job this answer belongs to, so late replies can be spotted
    int taskType;
    int step;
    int value1;      // task B: contacts traced | C: people vaccinated | D: clusters
    int value2;      // task D: largest cluster (0 for others)
    double startTime;
    double endTime;
};

// one analysis job. it carries its own id, the id of the job it depends on, and a
// status, exactly as described in the proposal.
struct Job {
    int taskId;                        // unique id of this job
    int prereqId;                      // the spread job for this day that it depends on
    int status;                        // WAITING, READY, IN-FLIGHT or COMPLETED
    int taskType;
    int step;
    std::vector<char> stateSnapshot;   // the day's state for the worker to analyse
};

static char letterOf(int taskType) {
    if (taskType == TASK_A) return 'A';
    if (taskType == TASK_B) return 'B';
    if (taskType == TASK_C) return 'C';
    return 'D';
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int myRank = 0;
    int numberOfProcesses = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
    MPI_Comm_size(MPI_COMM_WORLD, &numberOfProcesses);

    if (numberOfProcesses < 2) {
        if (myRank == 0) std::cout << "please run with at least 2 processes (a master plus a worker)\n";
        MPI_Finalize();
        return 1;
    }

    int numberOfNodes = 50000;
    std::string networkType = "scalefree";
    unsigned int randomSeed = 12345;
    bool simulateFailure = false;      // pass "failure" to demonstrate the recovery
    // A worker holding a job longer than this is treated as lost. It must be well above
    // the slowest normal job, otherwise a worker that is merely slow (for example when
    // several processes share few cores) would be declared dead by mistake. Override it
    // with timeout=<seconds> on the command line.
    double jobTimeoutSeconds = 30.0;
    if (argc >= 2) numberOfNodes = std::stoi(argv[1]);
    if (argc >= 3) networkType = argv[2];
    if (argc >= 4) randomSeed = static_cast<unsigned int>(std::stoul(argv[3]));
    for (int i = 1; i < argc; i = i + 1) {
        std::string option = argv[i];
        if (option == "failure") simulateFailure = true;
        if (option.rfind("timeout=", 0) == 0) {
            jobTimeoutSeconds = std::stod(option.substr(8));
        }
    }

    // every process builds the same network from the same seed
    EdgeList edges;
    if (networkType == "smallworld") edges = generateSmallWorldNetwork(numberOfNodes, 6, 0.1, randomSeed);
    else { networkType = "scalefree"; edges = generateScaleFreeNetwork(numberOfNodes, 3, randomSeed); }
    Graph graph = Graph::fromEdgeList(numberOfNodes, edges);

    SeirParameters parameters;
    parameters.transmissionProbability = 0.05;
    parameters.incubationProbability = 0.20;
    parameters.recoveryProbability = 0.10;
    parameters.numberOfSteps = 120;
    parameters.initialInfectedCount = 5;
    parameters.randomSeed = randomSeed;

    const int tracingDepth = 2;
    const int centralityIterations = 20;
    const int interventionInterval = 10;
    const int vaccinesPerRound = numberOfNodes / 100;

    // line everyone up, then use a shared time origin so timestamps compare
    MPI_Barrier(MPI_COMM_WORLD);
    double timeOrigin = MPI_Wtime();

    if (myRank != 0) {
        std::vector<char> stateBuffer(numberOfNodes);
        int jobsDoneHere = 0;
        while (true) {
            int header[3];
            MPI_Recv(header, 3, MPI_INT, 0, TAG_JOB, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            int taskType = header[0];
            int step = header[1];
            int taskId = header[2];
            if (taskType == STOP) break;

            MPI_Recv(stateBuffer.data(), numberOfNodes, MPI_CHAR, 0, TAG_STATE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            std::vector<HealthState> state(numberOfNodes);
            for (int i = 0; i < numberOfNodes; i = i + 1) state[i] = static_cast<HealthState>(stateBuffer[i]);

            jobsDoneHere = jobsDoneHere + 1;
            // if asked to, one worker pretends to fail on its third job: it simply never
            // answers. that lets us show the master noticing and giving the job to
            // somebody else, instead of the run hanging.
            if (simulateFailure && myRank == 1 && jobsDoneHere == 3) {
                std::printf("[worker %d] pretending to fail on task %d, sending no answer\n",
                            myRank, taskId);
                std::fflush(stdout);
                continue;
            }

            double start = MPI_Wtime() - timeOrigin;
            std::vector<int> chosenTargets;   // only task C fills this in
            JobResult result;
            result.taskId = taskId;
            result.taskType = taskType;
            result.step = step;
            result.value1 = 0;
            result.value2 = 0;

            if (taskType == TASK_B) {
                TracingResult tracing = runContactTracingFromInfectious(graph, state, tracingDepth);
                result.value1 = tracing.contactsTraced;
            } else if (taskType == TASK_C) {
                TargetingResult targeting = runInterventionTargeting(graph, state, vaccinesPerRound, centralityIterations);
                result.value1 = static_cast<int>(targeting.targets.size());
                chosenTargets = targeting.targets;   // sent back after the result below
            } else if (taskType == TASK_D) {
                ClusteringResult clustering = runOutbreakClustering(graph, state);
                result.value1 = clustering.clusterCount;
                result.value2 = clustering.largestClusterSize;
            }

            result.startTime = start;
            result.endTime = MPI_Wtime() - timeOrigin;
            MPI_Send(&result, sizeof(result), MPI_BYTE, 0, TAG_RESULT, MPI_COMM_WORLD);
            // task C also sends who it wants vaccinated, so the master can act on it
            if (taskType == TASK_C) {
                MPI_Send(chosenTargets.data(), static_cast<int>(chosenTargets.size()),
                         MPI_INT, 0, TAG_TARGETS, MPI_COMM_WORLD);
            }
        }
        MPI_Finalize();
        return 0;
    }

    // ================= MASTER (rank 0) =================
    std::filesystem::create_directories("results");
    std::ofstream timeline("results/timeline.csv");
    timeline << "rank,task,step,start,end\n";

    std::vector<HealthState> state(numberOfNodes, HealthState::susceptible);
    seedInitialInfected(state, parameters);

    std::vector<bool> workerBusy(numberOfProcesses, false);
    std::deque<Job> jobQueue;                         // jobs in state READY
    std::vector<Job> inFlightJob(numberOfProcesses);  // the job each worker is holding
    std::vector<double> dispatchedAt(numberOfProcesses, 0.0);
    int jobsCreated = 0;
    int jobsCompleted = 0;
    int jobsReEnqueued = 0;
    int nextTaskId = 0;
    std::vector<int> pendingVaccinations;   // task C's choices, applied on the next day
    int totalVaccinated = 0;

    // hand queued jobs to any idle workers. a job goes from READY to IN-FLIGHT here.
    auto assignJobs = [&]() {
        for (int worker = 1; worker < numberOfProcesses; worker = worker + 1) {
            if (jobQueue.empty()) break;
            if (workerBusy[worker]) continue;
            Job job = std::move(jobQueue.front());
            jobQueue.pop_front();
            job.status = STATUS_IN_FLIGHT;
            int header[3] = {job.taskType, job.step, job.taskId};
            MPI_Send(header, 3, MPI_INT, worker, TAG_JOB, MPI_COMM_WORLD);
            MPI_Send(job.stateSnapshot.data(), numberOfNodes, MPI_CHAR, worker, TAG_STATE, MPI_COMM_WORLD);
            workerBusy[worker] = true;
            dispatchedAt[worker] = MPI_Wtime();
            inFlightJob[worker] = std::move(job);
        }
    };

    // if a worker has held a job longer than the deadline we assume it is lost, and the
    // job goes back to READY so another worker can pick it up. this is the fault
    // handling promised in the proposal.
    auto reEnqueueTimedOutJobs = [&]() {
        double now = MPI_Wtime();
        for (int worker = 1; worker < numberOfProcesses; worker = worker + 1) {
            if (!workerBusy[worker]) continue;
            if (now - dispatchedAt[worker] < jobTimeoutSeconds) continue;

            Job lost = std::move(inFlightJob[worker]);
            std::printf("[master] worker %d timed out on task %d (%s), putting it back to READY\n",
                        worker, lost.taskId, nameOfStatus(STATUS_IN_FLIGHT));
            std::fflush(stdout);
            lost.status = STATUS_READY;
            jobQueue.push_front(std::move(lost));   // retry it next
            workerBusy[worker] = false;
            jobsReEnqueued = jobsReEnqueued + 1;
        }
    };

    // pick up finished results. if waitForOne is true, block until at least one
    // arrives (used to drain at the end and to push back when the queue is long).
    auto collectResults = [&](bool waitForOne) {
        bool gotOne = false;
        while (true) {
            int flag = 0;
            MPI_Status status;
            if (waitForOne && !gotOne) {
                MPI_Probe(MPI_ANY_SOURCE, TAG_RESULT, MPI_COMM_WORLD, &status);
                flag = 1;
            } else {
                MPI_Iprobe(MPI_ANY_SOURCE, TAG_RESULT, MPI_COMM_WORLD, &flag, &status);
            }
            if (!flag) break;
            JobResult result;
            MPI_Recv(&result, sizeof(result), MPI_BYTE, status.MPI_SOURCE, TAG_RESULT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            int from = status.MPI_SOURCE;
            gotOne = true;

            // task C always follows its result with the list of people to vaccinate, so
            // we must receive it whether or not we end up using it
            std::vector<int> targets;
            if (result.taskType == TASK_C) {
                MPI_Status targetStatus;
                MPI_Probe(from, TAG_TARGETS, MPI_COMM_WORLD, &targetStatus);
                int howMany = 0;
                MPI_Get_count(&targetStatus, MPI_INT, &howMany);
                targets.resize(howMany);
                MPI_Recv(targets.data(), howMany, MPI_INT, from, TAG_TARGETS,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }

            // a late answer for a job we already gave to somebody else: ignore it, so a
            // timed out job is never counted twice.
            if (!workerBusy[from] || inFlightJob[from].taskId != result.taskId) {
                continue;
            }

            // hold task C's choices until the next day, so the analysis of day t changes
            // the world at day t+1 rather than rewriting the day it was computed from
            if (result.taskType == TASK_C && !targets.empty()) {
                pendingVaccinations.insert(pendingVaccinations.end(),
                                           targets.begin(), targets.end());
            }

            workerBusy[from] = false;
            inFlightJob[from].status = STATUS_COMPLETED;
            jobsCompleted = jobsCompleted + 1;
            timeline << from << "," << letterOf(result.taskType) << ","
                     << result.step << "," << result.startTime << "," << result.endTime << "\n";
        }
    };

    for (int step = 0; step < parameters.numberOfSteps; step = step + 1) {
        // apply what task C decided on an earlier day 
        // This is the one piece of feedback in the system: the analysis of day t changes
        // the world from day t+1 onwards. Doing it here, before the spread, and never
        // inside the day it was computed from, is what keeps the pipeline from turning
        // into a cycle where A would have to wait for C.
        for (int node : pendingVaccinations) {
            if (state[node] == HealthState::susceptible) {
                state[node] = HealthState::recovered;   // vaccinated, so immune from now on
                totalVaccinated = totalVaccinated + 1;
            }
        }
        pendingVaccinations.clear();

        //  task A on the master (the pipeline spine) 
        double aStart = MPI_Wtime() - timeOrigin;
        std::vector<HealthState> nextState = state;
        #pragma omp parallel for schedule(static)
        for (int node = 0; node < numberOfNodes; node = node + 1) {
            nextState[node] = nextStateForNode(node, step, graph, state, parameters);
        }
        state = nextState;
        double aEnd = MPI_Wtime() - timeOrigin;
        timeline << 0 << "," << 'A' << "," << step << "," << aStart << "," << aEnd << "\n";

        // snapshot this day and queue the analysis jobs for it 
        std::vector<char> snapshot(numberOfNodes);
        for (int i = 0; i < numberOfNodes; i = i + 1) snapshot[i] = static_cast<char>(static_cast<int>(state[i]));

        // the spread job for this day is the prerequisite of all three analyses. it has
        // just finished, so they are created already READY rather than WAITING.
        int spreadJobId = nextTaskId;
        nextTaskId = nextTaskId + 1;

        jobQueue.push_back(Job{nextTaskId++, spreadJobId, STATUS_READY, TASK_B, step, snapshot});
        jobsCreated = jobsCreated + 1;
        jobQueue.push_back(Job{nextTaskId++, spreadJobId, STATUS_READY, TASK_D, step, snapshot});
        jobsCreated = jobsCreated + 1;
        if (step % interventionInterval == 0) {
            jobQueue.push_back(Job{nextTaskId++, spreadJobId, STATUS_READY, TASK_C, step, snapshot});
            jobsCreated = jobsCreated + 1;
        }

        // hand out work, pick up finished results, but do NOT wait, so task A can
        // race ahead to the next day while the workers are still busy. that is the
        // overlap that makes this task parallelism.
        // hand out the work, pick up anything finished, then loop straight on to the
        // next day. the false means do not block: if the master waited here the four
        // algorithms would run in phases instead of at the same time.
        assignJobs();
        collectResults(false);
        reEnqueueTimedOutJobs();

        // gentle back pressure so the queue and memory do not grow without bound
        while (static_cast<int>(jobQueue.size()) > 3 * (numberOfProcesses - 1)) {
            collectResults(true);
            reEnqueueTimedOutJobs();
            assignJobs();
        }
    }

    // finish any remaining analyses. we poll rather than block here so a worker that
    // never answers cannot stall the whole run: its job simply times out and is retried.
    while (jobsCompleted < jobsCreated) {
        assignJobs();
        collectResults(false);
        reEnqueueTimedOutJobs();
    }

    // tell the workers to stop
    for (int worker = 1; worker < numberOfProcesses; worker = worker + 1) {
        int header[3] = {STOP, 0, 0};
        MPI_Send(header, 3, MPI_INT, worker, TAG_JOB, MPI_COMM_WORLD);
    }

    timeline.close();

    std::cout << "task scheduler finished. " << jobsCreated << " analysis jobs ran on "
              << (numberOfProcesses - 1) << " worker processes.\n";
    std::cout << "jobs completed: " << jobsCompleted
              << " , jobs re-enqueued after a worker timed out: " << jobsReEnqueued << "\n";
    std::cout << "people vaccinated by task C (applied the day after it decided): "
              << totalVaccinated << "\n";
    std::cout << "wrote results/timeline.csv \n";

    MPI_Finalize();
    return 0;
}
