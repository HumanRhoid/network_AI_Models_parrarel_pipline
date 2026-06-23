#include "protocol.h"

// [서버의 전역 메모리 영역]
int worker_fds[100]; // 워커 장부
int client_fds[100]; // 클라이언트 장부
int worker_count = 0;
int client_count = 0;

struct sockaddr_in make_serv_addr()
{
    struct sockaddr_in serv = {0};
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = htonl(INADDR_ANY);
    serv.sin_port = htons(SERV_PORT);
    return serv;
}

int make_fd()
{
    int serv_fd;
    if ((serv_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket 문제 발생");
        exit(1);
    }
    return serv_fd;
}

void bind_socket_and_addr(const struct sockaddr* serv, int fd)
{
    if (bind(fd, serv, sizeof(struct sockaddr_in)) == -1)
    {
        perror("bind 문제발생");
        exit(1);
    }
}

int make_epoll()
{
    int epfd;
    if ((epfd = epoll_create1(0)) == -1)
    {
        perror("epoll 문제발생");
        exit(1);
    }
    return epfd;
}

void wait_client(int fd)
{
    if (listen(fd, 100) == -1)
    {
        perror("listen 문제발생");
        exit(1);
    }
}

void register_fd_to_epoll(int fd, int epfd)
{
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
}

// [직렬/병렬 오케스트레이션] 클라이언트별 진행상태(Job) 추적.
// 클라이언트는 한 번에 한 요청만 보내므로 client_fd를 작업 키로 쓴다.
#define MAX_JOBS 100
typedef struct
{
    int  in_use;
    int  client_fd;    // 결과를 돌려줄 클라이언트
    int  mode;         // MODE_W1 / MODE_SERIAL / MODE_PARALLEL
    int  next_worker;  // [직렬] 다음에 보낼 worker 인덱스(=단계번호)
    int  pending;      // [병렬] 아직 안 온 응답 수
    char acc[4096];    // 누적 결과(직렬 파이프라인 로그 / 병렬 수집)
} Job;
Job jobs[MAX_JOBS];

Job* job_find(int client_fd)
{
    for (int i = 0; i < MAX_JOBS; i++)
        if (jobs[i].in_use && jobs[i].client_fd == client_fd) return &jobs[i];
    return NULL;
}
Job* job_new(int client_fd, int mode)
{
    Job* j = job_find(client_fd);
    if (!j)
        for (int i = 0; i < MAX_JOBS; i++)
            if (!jobs[i].in_use) { j = &jobs[i]; break; }
    if (!j) return NULL;
    j->in_use = 1; j->client_fd = client_fd; j->mode = mode;
    j->next_worker = 0; j->pending = 0; j->acc[0] = '\0';
    return j;
}
void job_free(Job* j) { if (j) j->in_use = 0; }

// 결과 텍스트를 프레임에 담아 클라이언트로 회신 (기본 TCP send)
void reply_to_client(int client_fd, const char* text)
{
    MsgFrame out;
    memset(&out, 0, sizeof(out));
    out.type = TYPE_SERV;
    out.fd   = client_fd;
    snprintf(out.data, sizeof(out.data), "%s", text);
    out.data_len = (int)strlen(out.data);
    if (send(client_fd, &out, sizeof(out), 0) == -1)
        perror("클라이언트로 결과 회신 실패");
}

void handle_message(int fd, MsgFrame* msgFrame)
{
    if (msgFrame->type == TYPE_W1)
    {
        // 워커 등록
        worker_fds[worker_count++] = fd;
        printf("워커 등록됨 (fd:%d, 역할:%.15s) 총 %d개\n",
               fd, msgFrame->name[0] ? msgFrame->name : "기본", worker_count);
    }
    else if (msgFrame->type == TYPE_PROMPT)
    {
        client_fds[client_count++] = fd;
        printf("클라이언트 등록됨 (fd: %d)\n", fd);

        // [모드별 분배] 클라이언트 fd를 프레임에 '도장'으로 찍고(라우팅 키),
        //   단일/직렬(파이프라인)/병렬(브로드캐스트)로 나눠 보낸다.
        if (worker_count == 0)
        {
            reply_to_client(fd, "사용 가능한 워커가 없습니다.");
        }
        else
        {
            int mode = msgFrame->mode;
            msgFrame->fd = fd; // 라우팅 키 도장
            printf("질문 도착 (client fd:%d, mode:%d)\n", fd, mode);

            if (mode == MODE_PARALLEL)
            {
                Job* j = job_new(fd, MODE_PARALLEL);
                if (!j) { reply_to_client(fd, "서버 작업 한도 초과"); return; }
                j->pending = worker_count;
                for (int i = 0; i < worker_count; i++)
                    send(worker_fds[i], msgFrame, sizeof(MsgFrame), 0); // 전 워커 동시
                printf("  -> 병렬: 워커 %d개에 브로드캐스트\n", worker_count);
            }
            else if (mode == MODE_SERIAL)
            {
                Job* j = job_new(fd, MODE_SERIAL);
                if (!j) { reply_to_client(fd, "서버 작업 한도 초과"); return; }
                j->next_worker = 1; // 0번 먼저, 다음은 1번
                send(worker_fds[0], msgFrame, sizeof(MsgFrame), 0); // 파이프라인 시작
                printf("  -> 직렬: 워커0부터 파이프라인 시작(총 %d단계)\n", worker_count);
            }
            else // MODE_W1 (기본/단일)
            {
                Job* j = job_new(fd, MODE_W1);
                if (!j) { reply_to_client(fd, "서버 작업 한도 초과"); return; }
                j->pending = 1;
                send(worker_fds[0], msgFrame, sizeof(MsgFrame), 0);
                printf("  -> 단일: 워커0에 전달\n");
            }
        }
    }
    else if (msgFrame->type == TYPE_SERV)
    {
        // 워커 응답: 프레임에 찍힌 fd로 Job을 찾아 모드별 처리
        int cli = msgFrame->fd;
        Job* j = job_find(cli);

        if (!j) // 안전망: job 없으면 그대로 직송
        {
            send(cli, msgFrame, sizeof(MsgFrame), 0);
        }
        else if (j->mode == MODE_PARALLEL)
        {
            // 도착하는 대로 누적 (누가 답했는지 라벨 포함)
            char line[1600];
            snprintf(line, sizeof(line), "[%.15s]\n%.1300s\n\n",
                     msgFrame->name[0] ? msgFrame->name : "worker", msgFrame->data);
            strncat(j->acc, line, sizeof(j->acc) - strlen(j->acc) - 1);
            j->pending--;
            printf("  병렬 응답 수신 (남은 %d)\n", j->pending);
            if (j->pending <= 0) { reply_to_client(cli, j->acc); job_free(j); }
        }
        else if (j->mode == MODE_SERIAL)
        {
            int step = j->next_worker; // 1,2,3 ...
            char line[1600];
            snprintf(line, sizeof(line), "[단계%d:%.15s]\n%.1200s\n\n",
                     step, msgFrame->name[0] ? msgFrame->name : "worker", msgFrame->data);
            strncat(j->acc, line, sizeof(j->acc) - strlen(j->acc) - 1);

            if (step < worker_count) // 이 출력을 다음 워커의 입력으로
            {
                msgFrame->fd = cli;            // 라우팅 키 유지
                j->next_worker = step + 1;
                send(worker_fds[step], msgFrame, sizeof(MsgFrame), 0);
                printf("  직렬 단계%d 완료 -> 워커%d로 전달\n", step, step);
            }
            else // 마지막 단계 -> 파이프라인 전체 회신
            {
                reply_to_client(cli, j->acc); job_free(j);
                printf("  직렬 파이프라인 완료\n");
            }
        }
        else // MODE_W1 (단일)
        {
            char line[1600];
            snprintf(line, sizeof(line), "[%.15s] %.1300s",
                     msgFrame->name[0] ? msgFrame->name : "AI", msgFrame->data);
            reply_to_client(cli, line); job_free(j);
            printf("결과 -> 클라이언트(fd:%d) 회신\n", cli);
        }
    }
}

void epoll_loop(int fd, int epfd, MsgFrame* msgFrame)
{
    struct epoll_event events[10];
    while (1)
    {
        int n = epoll_wait(epfd, events, 10, -1);
        for (int i = 0; i < n; i++)
        {
            if (events[i].data.fd == fd)
            {
                struct sockaddr_in cli;
                socklen_t cli_len = sizeof(cli);
                int cli_fd = accept(fd, (struct sockaddr*)&cli, &cli_len);
                if (cli_fd == -1)
                {
                    perror("epoll accept 문제발생");
                    continue;
                }
                register_fd_to_epoll(cli_fd, epfd);
            }
            else
            {
                int cur = events[i].data.fd;
                // 기본 TCP 수신: 한 번의 recv로 한 프레임을 받는다고 가정
                ssize_t r = recv(cur, msgFrame, sizeof(MsgFrame), 0);
                if (r <= 0) // 0=연결 종료, -1=오류
                {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, cur, NULL);
                    close(cur);
                    continue;
                }
                handle_message(cur, msgFrame);
            }
        }
    }
}

int main()
{
    MsgFrame msgFrame;
    struct sockaddr_in serv = make_serv_addr();
    int fd = make_fd();
    bind_socket_and_addr((struct sockaddr*)&serv, fd);
    int epfd = make_epoll();
    wait_client(fd);
    register_fd_to_epoll(fd, epfd);
    epoll_loop(fd, epfd, &msgFrame);
}
