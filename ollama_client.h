#pragma once
//=================================================================
//  ollama_client.h  —  로컬 LLM(Ollama)에 HTTP로 질의하는 클라이언트
//-----------------------------------------------------------------
//  [출처/참고]AI
//   - Ollama REST API 문서: https://github.com/ollama/ollama (docs/api.md)
//     · POST /api/generate  요청  {"model","prompt","stream":false}
//     · 응답 JSON           {"response":"...","done":true, ...}
//   - HTTP 요청/응답 처리와 JSON 파싱은 외부 라이브러리 없이 직접 구현.
//     (TCP 소켓으로 직접 HTTP를 말한다 = 본 과제의 IPC 학습 취지)
//
//  worker 가 받은 프롬프트를 이 함수로 Ollama 에 넘겨 답변을 얻는다.
//  연결/응답 실패 시 -1 을 돌려주어, 호출측이 폴백 응답을 쓰게 한다.
//  (학과 서버 네트워크 장애 경고 대비: AI가 죽어도 IPC 왕복은 유지)
//=================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/time.h>
#include <errno.h>

// 접속 대상 기본값 (환경변수로 덮어쓰기 가능)
#define OLLAMA_HOST_DEFAULT  "127.0.0.1"
#define OLLAMA_PORT_DEFAULT  11434
#define OLLAMA_MODEL_DEFAULT "qwen2.5:0.5b"

// ── JSON 문자열 이스케이프: src -> dst (",\,제어문자 처리) ──
static int json_escape(char* dst, size_t cap, const char* src)
{
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0'; i++)
    {
        unsigned char c = (unsigned char)src[i];
        const char* rep = NULL;
        char buf[8];
        switch (c)
        {
            case '"':  rep = "\\\""; break;
            case '\\': rep = "\\\\"; break;
            case '\n': rep = "\\n";  break;
            case '\r': rep = "\\r";  break;
            case '\t': rep = "\\t";  break;
            default:
                if (c < 0x20) { snprintf(buf, sizeof(buf), "\\u%04x", c); rep = buf; }
        }
        if (rep)
        {
            size_t L = strlen(rep);
            if (j + L >= cap) break;
            memcpy(dst + j, rep, L);
            j += L;
        }
        else
        {
            if (j + 1 >= cap) break;
            dst[j++] = (char)c; // 한글 등 UTF-8 바이트는 그대로
        }
    }
    dst[j] = '\0';
    return (int)j;
}

// ── JSON에서 "key":"value" 의 value 추출 + 기본 이스케이프 해제 ──
//    성공 1 / 실패 0
static int json_extract_string(const char* json, const char* key,
                               char* out, size_t cap)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char* p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return 0;
    p++; // 여는 따옴표 다음

    size_t j = 0;
    while (*p && *p != '"')
    {
        if (*p == '\\')
        {
            p++;
            char c;
            switch (*p)
            {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '"': c = '"';  break;
                case '\\': c = '\\'; break;
                case '/': c = '/';  break;
                case 'u': // \uXXXX -> UTF-8 (BMP)
                    if (p[1] && p[2] && p[3] && p[4])
                    {
                        char hex[5] = { p[1], p[2], p[3], p[4], 0 };
                        unsigned int cp = (unsigned int)strtol(hex, NULL, 16);
                        p += 4;
                        if (cp < 0x80) { if (j + 1 < cap) out[j++] = (char)cp; }
                        else if (cp < 0x800)
                        {
                            if (j + 2 < cap) { out[j++] = (char)(0xC0 | (cp >> 6));
                                               out[j++] = (char)(0x80 | (cp & 0x3F)); }
                        }
                        else
                        {
                            if (j + 3 < cap) { out[j++] = (char)(0xE0 | (cp >> 12));
                                               out[j++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                               out[j++] = (char)(0x80 | (cp & 0x3F)); }
                        }
                        p++;
                        continue;
                    }
                    c = 'u'; break;
                default: c = *p;
            }
            if (*p == '\0') break;
            if (j + 1 < cap) out[j++] = c;
            p++;
        }
        else
        {
            if (j + 1 < cap) out[j++] = *p;
            p++;
        }
    }
    out[j] = '\0';
    return 1;
}

// ── Ollama에 prompt 질의 -> out에 답변 채움. 성공 0 / 실패 -1 ──
static int ask_ollama(const char* prompt, char* out, size_t out_cap)
{
    const char* host  = getenv("OLLAMA_HOST_ADDR"); if (!host)  host  = OLLAMA_HOST_DEFAULT;
    const char* model = getenv("OLLAMA_MODEL");     if (!model) model = OLLAMA_MODEL_DEFAULT;
    int port = OLLAMA_PORT_DEFAULT;
    const char* pe = getenv("OLLAMA_PORT"); if (pe) port = atoi(pe);

    // 1) 소켓 연결
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) { close(fd); return -1; }

    // 응답 지연/멈춤 대비 타임아웃 (worker가 영원히 안 멈추도록)
    struct timeval tv = { .tv_sec = 120, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(fd); return -1; }

    // 2) JSON 본문
    char esc[8192];
    json_escape(esc, sizeof(esc), prompt);
    char body[9000];
    int blen = snprintf(body, sizeof(body),
        "{\"model\":\"%s\",\"prompt\":\"%s\",\"stream\":false}", model, esc);

    // 3) HTTP 요청 (Connection: close -> 응답 끝을 EOF로 판단)
    char req[10000];
    int rlen = snprintf(req, sizeof(req),
        "POST /api/generate HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n%s", host, port, blen, body);

    if (send(fd, req, (size_t)rlen, 0) < 0) { close(fd); return -1; }

    // 4) 응답 전체 읽기 (EOF까지). "response" 필드는 "context"보다 앞에 오므로
    //    버퍼가 가득 차 뒤가 잘려도 답변 추출엔 지장 없음.
    static char resp[262144]; // 256KB
    size_t total = 0;
    ssize_t n;
    while ((n = recv(fd, resp + total, sizeof(resp) - 1 - total, 0)) > 0)
    {
        total += (size_t)n;
        if (total >= sizeof(resp) - 1) break;
    }
    close(fd);
    if (total == 0) return -1;
    resp[total] = '\0';

    // 5) HTTP 헤더 끝(\r\n\r\n) 다음이 본문
    char* body_start = strstr(resp, "\r\n\r\n");
    if (!body_start) return -1;
    body_start += 4;

    // 6) JSON에서 response 추출
    if (!json_extract_string(body_start, "response", out, out_cap)) return -1;
    return 0;
}
