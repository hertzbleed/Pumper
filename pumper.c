#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <io.h>         //_get_osfhandle, _fileno
#include <windows.h>
#include <bcrypt.h>     //BCryptGenRandom
#pragma comment(lib, "bcrypt.lib")

#define CHUNK_SIZE (4 * 1024 * 1024) // 4 MiB per cycle

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr,
            "Usage: %s <filename> <target_size_bytes> <mode>\n"
            " mode: 0 = zero padding, 1 = random padding (BCryptGenRandom)\n",
            argv[0]);
        return 1;
    }

    const char* filename = argv[1];
    long long target_size = _atoi64(argv[2]);
    int random_mode = atoi(argv[3]);

    if (target_size <= 0) {
        fprintf(stderr, "Invalid target size: %s\n", argv[2]);
        return 2;
    }

    FILE* fp = NULL;
    errno_t ferr = fopen_s(&fp, filename, "r+b"); // opening for read+write, binary
    if (ferr != 0 || !fp) {
        //creating the file if it doesn't exist for demonstration purposes
        if (ferr != 0) {
            fprintf(stderr, "Cannot open file '%s' for update (errno=%d). Attempting to create.\n", filename, ferr);
        }
        ferr = fopen_s(&fp, filename, "w+b");
        if (ferr != 0 || !fp) {
            perror("fopen_s");
            return 3;
        }
    }

    //figure out current size 
    if (_fseeki64(fp, 0, SEEK_END) != 0) {
        perror("_fseeki64");
        fclose(fp);
        return 4;
    }
    long long current_size = _ftelli64(fp);
    if (current_size < 0) {
        perror("_ftelli64");
        fclose(fp);
        return 5;
    }

    if (current_size >= target_size) {
        printf("File already >= target size (%lld >= %lld). No action taken.\n", current_size, target_size);
        fclose(fp);
        return 0;
    }

    long long bytes_to_add = target_size - current_size;
    printf("Current size: %lld bytes. Target size: %lld bytes. Appending %lld bytes...\n",
        current_size, target_size, bytes_to_add);

    unsigned char* buf = (unsigned char*)malloc(CHUNK_SIZE);
    if (!buf) {
        perror("malloc");
        fclose(fp);
        return 6;
    }

    //seed rand fallback
    srand((unsigned)GetTickCount64());

    while (bytes_to_add > 0) {
        size_t chunk = (bytes_to_add > CHUNK_SIZE) ? CHUNK_SIZE : (size_t)bytes_to_add;

        if (random_mode) {
            // Use BCryptGenRandom for secure random bytes
            NTSTATUS status = BCryptGenRandom(NULL, buf, (ULONG)chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            if (status != 0) {
                //fall back to rand() in case of failure
                fprintf(stderr, "Warning: BCryptGenRandom failed (0x%08x). Falling back to rand().\n", (unsigned)status);
                for (size_t i = 0; i < chunk; ++i) buf[i] = (unsigned char)(rand() & 0xFF);
            }
        }
        else {
            //zero padding aka default method
            memset(buf, 0, chunk);
        }

        size_t written = fwrite(buf, 1, chunk, fp);
        if (written != chunk) {
            perror("fwrite");
            free(buf);
            fclose(fp);
            return 7;
        }

        bytes_to_add -= written;
    }

    //C stdio buffers
    fflush(fp);

    //ensure data committed to disk
    int fd = _fileno(fp);
    if (fd >= 0) {
        HANDLE h = (HANDLE)_get_osfhandle(fd);
        if (h != INVALID_HANDLE_VALUE) {
            if (!FlushFileBuffers(h)) {
                fprintf(stderr, "Warning: FlushFileBuffers failed (GetLastError=0x%08x)\n", (unsigned)GetLastError());
            }
        }
    }

    // reposition to end and report the final size
    if (_fseeki64(fp, 0, SEEK_END) == 0) {
        long long final_size = _ftelli64(fp);
        printf("Done. Final file size: %lld bytes.\n", final_size);
    }
    else {
        printf("Done. (Could not query final size.)\n");
    }

    free(buf);
    fclose(fp);
    return 0;
}
