#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <fcntl.h>
#include<pthread.h>
#include <mqueue.h>
#include <math.h>
#include <stdbool.h>

// region variables

// Task 2
sem_t is_read;
sem_t is_written;
bool run = true;
int counter = 0;
const int SIZE = 1000;
const char* name = "buffer";
// Task 3
#define QUEUE_NAME "pi_queue"
#define MESSAGE_SIZE sizeof(double)
double sum = 0;
sem_t waiter;
pthread_mutex_t lock;

//endregion

//region helper functions

void * writer() {
    long currentPosition = 0;
    while(run) {
        sem_wait(&is_read);

        FILE * file = fopen("input.txt", "r");
        int shm_fd;
        void * ptr;
        char read[SIZE];

        shm_fd = shm_open(name, O_CREAT | O_RDWR, 0666);
        ftruncate(shm_fd, SIZE);
        ptr = mmap(0, SIZE, PROT_WRITE, MAP_SHARED, shm_fd, 0);
        fseek(file, currentPosition, SEEK_SET);
        fread(read, SIZE, 1, file);
        currentPosition = ftell(file);
        fclose(file);
        sprintf(ptr, "%s", read);

        sem_post(&is_written);

        counter++;
    }
}

void * reader() {
    while(run) {
        sem_wait(&is_written);

        FILE * file = fopen("output.txt", "a+");
        int shm_fd;
        void * ptr;

        shm_fd = shm_open(name, O_CREAT | O_RDWR, 0666);
        ftruncate(shm_fd, SIZE);
        ptr = mmap(0, SIZE, PROT_WRITE, MAP_SHARED, shm_fd, 0);
        fwrite((char *) ptr, SIZE,1, file);
        fclose(file);

        sem_post(&is_read);

        if(counter >= 10) run = false;
    }
}

double getPi(int pointsNum) {
    int squareSize = 10000;
    int circle_center_x = squareSize / 2;
    int circle_center_y = squareSize / 2;
    int radius = squareSize / 2;
    int pointsInCircle = 0;
    double x_distance;
    double y_distance;

    srand(getpid());

    for(int i = 0; i < pointsNum; i++) {

        x_distance = abs(circle_center_x - rand() % (squareSize + 1));
        y_distance = abs(circle_center_y - rand() % (squareSize + 1));

        if(sqrt(((x_distance * x_distance) + (y_distance * y_distance))) < radius)
            pointsInCircle++;
    }
    return (4.0 * pointsInCircle / pointsNum);
}

void send(int threadNum, int pointsNum) {
    char queueName[20];
    mqd_t queue;
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_msgsize = MESSAGE_SIZE;
    attr.mq_curmsgs = 0;
    attr.mq_maxmsg = 10;

    sprintf(queueName, "/pi_queue_%d", threadNum);
    queue = mq_open(queueName, O_CREAT | O_WRONLY, 0666, &attr);
    double pi = getPi(pointsNum);
    mq_send(queue, (char*) &pi, sizeof(double), 0);

    mq_close(queue);
    mq_unlink(QUEUE_NAME);
}

void receive(int threadNum) {
    char queueName[20];
    double pi;
    mqd_t queue;
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_msgsize = MESSAGE_SIZE;
    attr.mq_curmsgs = 0;
    attr.mq_maxmsg = 100;

    sprintf(queueName, "/pi_queue_%d", threadNum);
    queue = mq_open(queueName, O_CREAT | O_RDONLY, 0666, &attr);
    mq_receive(queue, (char *) &pi, sizeof(double), NULL);

    pthread_mutex_lock(&lock);
    sum += pi;
    pthread_mutex_unlock(&lock);

    mq_close(queue);
    mq_unlink(QUEUE_NAME);

    sem_post(&waiter);
}

//endregion

//region task functions

void task1() {
    int fd[2]; //two ends of a pipe - read and write
    int bufferSize = 100;
    pid_t forkId;
    pipe(fd);
    forkId = fork();
    if(forkId < 0) {
        printf("Error");
    } else if(forkId > 0) { // Parent process - printing the output
        char output[bufferSize];
        ssize_t n = read(fd[0], output, sizeof(output) - 1);
        if (n == -1) {
            perror("Error while reading");
            exit(EXIT_FAILURE);
        }
        output[n] = '\0';
        close(fd[0]);
        printf("The message: %s", output);
    } else { //  Child process - getting input
        char input[bufferSize];
        printf("Enter the message: ");
        fflush(stdout);
        if (fgets(input, bufferSize, stdin) == NULL) {
            perror("Error reading from standard input");
            exit(EXIT_FAILURE);
        }
        if (write(fd[1], input, bufferSize) == -1) {
            perror("Error writing to pipe");
            exit(EXIT_FAILURE);
        }
        close(fd[1]);
    }
}

void task2() {
    pthread_t r;
    pthread_t w;

    sem_init(&is_read, 0, 1);
    sem_init(&is_written, 0, 0);

    pthread_create(&r,NULL, reader, NULL);
    pthread_create(&w,NULL, writer, NULL);

    pthread_join(r, NULL);
    pthread_join(w, NULL);
}

void task3() {
    int threadNum = 5;
    int pointsNum = 1000000;

    sem_init(&waiter, 0, 0);

    for (int i = 0; i < threadNum; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            printf("Error");
        } else if(pid > 0) {
            receive(i);
        } else {
            send(i, pointsNum);
        }
    }
    for (int i = 0; i < threadNum; i++) {
        sem_wait(&waiter);
    }
    mq_unlink(QUEUE_NAME);
    double averagePi = sum / threadNum;
    printf("Estimated Pi value: %f\n", averagePi);
}

//endregion

int main() {
    task1();
    task2();
    task3();
    return 0;
}
