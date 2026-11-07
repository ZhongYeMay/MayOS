/* Isolated unit test for bounded HTML/HTTP text preview used by both kernels. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/browser_text.h"

int main(void) {
    char buf[1024];
    const char *plain = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n"
                        "Content-Length: 12\r\n\r\nHello MayOS!";
    browse_preview(plain, buf, sizeof(buf));
    assert(strstr(buf, "HTTP 200") != NULL);
    assert(strstr(buf, "Hello MayOS!") != NULL);
    assert(strstr(buf, "Content-Length") == NULL);

    const char *html = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
       "<html><head><style>SECRET_STYLES</style></head>"
       "<body><h1>MayOS &amp; Friends</h1><script>SECRET_SCRIPTS</script>"
       "<p>Native <strong>text</strong> preview<br>works &lt;now&gt;.</p>"
       "</body></html>";
    browse_preview(html, buf, sizeof(buf));
    assert(strstr(buf, "MayOS & Friends") != NULL);
    assert(strstr(buf, "Native text preview") != NULL);
    assert(strstr(buf, "works <now>.") != NULL);
    assert(strstr(buf, "SECRET_STYLES") == NULL);
    assert(strstr(buf, "SECRET_SCRIPTS") == NULL);
    assert(strstr(buf, "<h1>") == NULL);

    char tiny[14];
    memset(tiny, 0xaa, sizeof(tiny));
    browse_preview(html, tiny, 13);
    assert(tiny[12] == '\0' || strlen(tiny) <= 12);
    assert((unsigned char)tiny[13] == 0xaa);
    browse_preview("Network not ready", buf, sizeof(buf));
    assert(!strcmp(buf, "Network not ready"));
    browse_preview("", buf, sizeof(buf));
    assert(!strcmp(buf, ""));
    puts("PASS: HTTP status, header stripping, safe HTML text, entities, no scripts, bounds");
    return 0;
}
