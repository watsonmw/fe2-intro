#include "annotation.h"

#include <SDL2/SDL.h>
#include <stdio.h>

Annotations::Annotations() {
    annotations.init(10);
}

Annotations::~Annotations() {

}

bool miswhitespace(char c) {
    return (c == '\n' || c == '\r' || c == '\t' || c == ' ');
}

void mstrtoi16(const char* start, const char* end, int* out) {
    int val = 0;
    int begin = 1;
    int base = 16;

    for (const char* pos = start; pos < end; pos++) {
        char c = *pos;
        if (begin && miswhitespace(c)) {
            continue;
        }

        begin = 0;

        int p = 0;
        if (c >= '0' && c <= '9') {
            p = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            p = 10 + c - 'a';
        } else if (c >= 'A' && c <= 'F') {
            p = 10 + c - 'A';
        } else {
            continue;
        }

        val = (val * base) + p;
    }

    *out = val;
}

enum ParseState {
    ParseState_LINE_OFFSET_BEGIN,
    ParseState_LINE_OFFSET,
    ParseState_LINE_COMMENT_QUOTE,
    ParseState_LINE_COMMENT,
};

void Annotations::load(const char* filepath) {
    SDL_RWops *file = SDL_RWFromFile(filepath, "r+");
    if (file == NULL) {
        MLogf("Unable to find %s", filepath);
        return;
    }

    uint64_t size = SDL_RWsize(file);

    char* data = (char*)malloc(size);
    size_t bytesRead = SDL_RWread(file, data, 1, size);
    SDL_RWclose(file);

    ParseState parseState = ParseState_LINE_OFFSET;

    char* valStart = data;
    bool openQuote = false;

    Annotation annotation;

    for (int i = 0; i < bytesRead; ++i) {
        char c = data[i];
        switch (parseState) {
            case ParseState_LINE_OFFSET_BEGIN:
                if (!miswhitespace(c)) {
                    valStart = data + i;
                    parseState = ParseState_LINE_OFFSET;
                }
                break;
           case ParseState_LINE_OFFSET:
                if (c == ',') {
                    mstrtoi16(valStart, data + i, &annotation.offset);
                    parseState = ParseState_LINE_COMMENT_QUOTE;
                } else if (c == '\n' || c == '\r') {
                    parseState = ParseState_LINE_OFFSET;
                }
                break;
            case ParseState_LINE_COMMENT_QUOTE:
                if (c == '"') {
                    openQuote = true;
                    parseState = ParseState_LINE_COMMENT;
                    valStart = data + i + 1;
                } else if (!miswhitespace(c)) {
                    valStart = data + i + 1;
                    parseState = ParseState_LINE_COMMENT;
                }
                break;
            case ParseState_LINE_COMMENT:
                bool doAdd = false;
                if (openQuote) {
                    if (c == '"' || c == '\n' || c == '\r') {
                        doAdd = true;
                    }
                } else {
                    if (c == '\n' || c == '\r') {
                        doAdd = true;
                    }
                }
                if (doAdd) {
                    openQuote = false;
                    parseState = ParseState_LINE_OFFSET_BEGIN;
                    int strSize = data + i - valStart + 1;
                    annotation.comment = (char*)malloc(strSize);
                    memcpy(annotation.comment, valStart, strSize - 1);
                    annotation.comment[strSize-1] = 0;
                    annotations.add(annotation);
                }
                break;
        }
    }

    free(data);
}

void Annotations::save(const char* filepath) {
    SDL_RWops *file = SDL_RWFromFile(filepath, "w+b");
    if (file == NULL) {
        MLogf("Unable to open %s for writing", filepath);
        return;
    }

    const int bufferSize = 1024;
    char buffer[bufferSize];

    for (int i = 0; i < annotations.size(); ++i) {
        Annotation annotation = annotations.get(i);
        int n = snprintf(buffer, bufferSize, "%x,\"%s\"\n", annotation.offset, annotation.comment);
        SDL_RWwrite(file, buffer, n, 1);
    }

    SDL_RWclose(file);
}

bool Annotations::addRaw(const char* comment) {
    int offset = 0;
    const char* current = comment;
    while (*current != 0 && !miswhitespace(*current)) {
        current++;
    }
    mstrtoi16(comment, current, &offset);

    if (*current == 0) {
        remove(offset);
    } else {
        add(offset, current + 1);
    }

    return true;
}

void Annotations::add(int offset, const char* comment) {
    Annotation annotation;
    int strSize = strlen(comment) + 1;
    annotation.offset = offset;
    annotation.comment = (char*)malloc(strSize);
    memcpy(annotation.comment, comment, strSize);
    annotations.add(annotation);
}

void Annotations::remove(int offset) {
    int toRemoveOffset = -1;
    for (int i = 0; i < annotations.size(); ++i) {
        Annotation annotation = annotations.get(i);
        if (annotation.offset == offset) {
            toRemoveOffset = i;
        }
    }
    annotations.remove(toRemoveOffset);
}

char* Annotations::getForOffset(int offset) {
    for (int i = 0; i < annotations.size(); ++i) {
        Annotation annotation = annotations.get(i);
        if (annotation.offset == offset) {
            return annotation.comment;
        }
    }
    return 0;
}

void TestAnnotations() {
    Annotations test;
    test.load("test/annotations_in.csv");

    MLog(test.getForOffset(0x1111));
    MLog(test.getForOffset(0x1112));
    MLog(test.getForOffset(0x1113));

    test.add(0xaaaa, "some comment4");

    MLog(test.getForOffset(0xaaaa));

    test.save("test/annotations_out.csv");

    Annotations test2;
    test2.load("test/annotations_out.csv");
    MLog(test2.getForOffset(0xaaaa));
}