#include "protocol.h"

void check_arg_count(int argc)
{
    if (argc != 3)
    {
        printf("client <서버 ip 주소> <닉네임> 형식으로 입력하세요.");
        exit(1);
    }
}

void fill_header(MsgFrame* msgFrame, const char* name)
{
    memset(msgFrame, 0, sizeof(MsgFrame));
    msgFrame->type = TYPE_PROMPT;
    strncpy(msgFrame->name, name, sizeof(msgFrame->name) - 1);
}

struct sockaddr_in make_serv_addr(const char* ip)
{
    struct sockaddr_in serv = {0};
    serv.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &serv.sin_addr);
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

void connect_to_server(int serv_fd, struct sockaddr_in serv)
{
    if (connect(serv_fd, (struct sockaddr*)&serv, sizeof(serv)) == -1)
    {
        perror("connect 문제 발생");
        exit(1);
    }
}

void exit_prompt(int serv_fd, char* line)
{
    if (strcmp(line, "q\n") == 0 || strcmp(line, "/exit\n") == 0)
    {
        close(serv_fd);
        exit(0);
    }
}

// 매 질문마다 처리 모드를 고른다: 1=단일 2=직렬 3=병렬 (q=종료)
int choose_mode(int serv_fd)
{
    char line[64];
    printf("\n모드 선택 [1]단일 [2]직렬(파이프라인) [3]병렬(여러워커) (q=종료) > ");
    fflush(stdout);
    if (fgets(line, sizeof(line), stdin) == NULL)
    {
        close(serv_fd);
        exit(0);
    }
    exit_prompt(serv_fd, line); // q, /exit 처리
    if (line[0] == '2') return MODE_SERIAL;
    if (line[0] == '3') return MODE_PARALLEL;
    return MODE_W1;
}

void prompt(MsgFrame* msgFrame, int serv_fd)
{
    char line[1024];
    msgFrame->data[0] = '\0';

    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        exit_prompt(serv_fd, line);
        if (strcmp(line, "\n") == 0)
            break;
        if (strlen(msgFrame->data) + strlen(line) < sizeof(msgFrame->data))
            strcat(msgFrame->data, line);
        else
        {
            printf("입력 버퍼 초과... 입력 중단");
            break;
        }
    }
    msgFrame->data_len = (int)strlen(msgFrame->data);
}

void send_prompt_to_server(int serv_fd, MsgFrame* msgFrame)
{
    printf("질문 입력(빈 줄 엔터로 전송):\n");
    prompt(msgFrame, serv_fd);

    if (send(serv_fd, msgFrame, sizeof(MsgFrame), 0) == -1) // 기본 TCP send
    {
        perror("send 중 오류발생");
        exit(1);
    }
}

void recv_result_from_server(int serv_fd, MsgFrame* msgFrame)
{
    ssize_t r = recv(serv_fd, msgFrame, sizeof(MsgFrame), 0); // 기본 TCP recv
    if (r <= 0) // 0=서버 종료, -1=오류
    {
        perror("recv 받기 문제");
        exit(1);
    }
    printf("\n===== 결과 =====\n%s\n================\n", msgFrame->data);
}

int main(int argc, char** argv)
{
    MsgFrame msgFrame;
    check_arg_count(argc);

    struct sockaddr_in serv = make_serv_addr(argv[1]);
    int serv_fd = make_fd();

    connect_to_server(serv_fd, serv);
    while (1)
    {
        fill_header(&msgFrame, argv[2]);
        msgFrame.mode = choose_mode(serv_fd); // 단일/직렬/병렬 선택

        send_prompt_to_server(serv_fd, &msgFrame);
        if (msgFrame.data_len == 0)
            continue;

        recv_result_from_server(serv_fd, &msgFrame);
    }
}
