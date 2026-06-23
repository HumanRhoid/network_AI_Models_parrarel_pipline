#include "protocol.h"
#include "ollama_client.h" // 로컬 Ollama연동 AI

void fill_header(MsgFrame* msgFrame)
{
    memset(msgFrame, 0, sizeof(MsgFrame));
    msgFrame->type = TYPE_W1;
}

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3)
    {
        printf("worker <브로커IP> [역할설명] 형식으로 입력하세요.\n");
        printf("  예) ./worker 127.0.0.1 \"질문에 친절하고 간결히 답하라\"\n");
        exit(1);
    }
    const char* role = (argc == 3) ? argv[2] : ""; // 직렬/병렬용 역할(페르소나)
    char label[16];
    if (role[0])
    {
        int n = (int)strlen(role); if (n > 15) n = 15;
        // 한글(UTF-8 멀티바이트)을 중간에서 자르지 않도록 경계까지 back-off
        while (n > 0 && ((unsigned char)role[n] & 0xC0) == 0x80) n--;
        memcpy(label, role, n); label[n] = '\0';
    }
    else snprintf(label, sizeof(label), "worker");

    MsgFrame msgFrame;
    fill_header(&msgFrame);
    strncpy(msgFrame.name, label, sizeof(msgFrame.name) - 1); // 등록 시 역할 라벨 보고

    int worker_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (worker_fd == -1)
    {
        perror("socket 문제발생");
        exit(1);
    }

    struct sockaddr_in broker = {0};
    broker.sin_family = AF_INET;
    inet_pton(AF_INET, argv[1], &broker.sin_addr);
    broker.sin_port = htons(SERV_PORT);

    if (connect(worker_fd, (struct sockaddr*)&broker, sizeof(broker)) == -1)
    {
        perror("connect 문제발생");
        exit(1);
    }

    // 등록 메시지 전송 (기본 TCP send)
    if (send(worker_fd, &msgFrame, sizeof(MsgFrame), 0) == -1)
    {
        perror("send 문제발생");
        exit(1);
    }

    // 등록 후, 서버가 넘겨주는 프롬프트를 계속 받아 답을 회신한다.
    while (1)
    {
        MsgFrame in;
        ssize_t r = recv(worker_fd, &in, sizeof(in), 0); // 기본 TCP recv
        if (r <= 0) // 0=서버가 연결 종료, -1=오류
        {
            printf("워커 %s: 서버 연결 종료\n", argv[1]);
            break;
        }

        char question[1428];
        strncpy(question, in.data, sizeof(question) - 1);
        question[sizeof(question) - 1] = '\0';

        in.type = TYPE_SERV; // "결과" 표식: 서버는 이걸 보고 클라이언트로 되돌린다
        // in.fd 는 서버가 찍어준 '원래 클라이언트 fd' -> 그대로 둬야 라우팅됨
        strncpy(in.name, label, sizeof(in.name) - 1); // 누가 답했는지 라벨
        in.name[sizeof(in.name) - 1] = '\0';

        // [AI 연동 + 역할] 역할이 있으면 프롬프트 앞에 붙여 페르소나/단계 부여.
        //   직렬에서는 question이 앞 워커의 출력(=다음 입력)이 된다.
        char prompt[1700];
        if (role[0])
            snprintf(prompt, sizeof(prompt), "%.300s\n\n[입력]\n%.1300s", role, question);
        else
            snprintf(prompt, sizeof(prompt), "%.1400s", question);

        char answer[1428];
        if (ask_ollama(prompt, answer, sizeof(answer)) == 0)
            snprintf(in.data, sizeof(in.data), "%.1400s", answer); // 원답변만
        else
            // [폴백] Ollama 미동작/네트워크 장애 시 (IPC 왕복은 유지)
            snprintf(in.data, sizeof(in.data),
                     "[%.15s 폴백] AI 응답 실패. 입력: \"%.900s\"", label, question);
        in.data_len = (int)strlen(in.data);

        if (send(worker_fd, &in, sizeof(in), 0) == -1)
        {
            perror("worker 응답 send 오류");
            break;
        }
    }
    close(worker_fd);
}
