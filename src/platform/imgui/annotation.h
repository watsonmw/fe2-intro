#ifndef TOOL_ANNOTATION_H
#define TOOL_ANNOTATION_H

#include "mcpplib.h"

struct Annotation {
    int offset;
    char* comment;
};

class Annotations {
public:
    Annotations();
    ~Annotations();

    void load(const char* filepath);
    void save(const char* filepath);

    void add(int offset, const char* comment);
    void remove(int offset);
    bool addRaw(const char* comment);

    char* getForOffset(int offset);

private:
    MValArray<Annotation> annotations;
};

void TestAnnotations();

#endif
