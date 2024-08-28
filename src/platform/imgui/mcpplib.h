#ifndef MCPPLIB_H
#define MCPPLIB_H

#include "string.h"
#include "stdlib.h"
#include "mlib.h"

template <typename T>
class MPtrArray
{
public:
    MPtrArray(int initialSize)
            : _p(0),
              _size(0),
              _allocSize(0)
    {
        _allocSize = initialSize;
        _p = (T**)malloc(sizeof(T*) * _allocSize);
    }

    MPtrArray()
            : _p(0),
              _size(0),
              _allocSize(0)
    {
        _allocSize = 32;
        _p = (T**)malloc(sizeof(T*) * _allocSize);
    }

    ~MPtrArray() {
        free(_p); _p = 0;
    }

    T** data() const { return _p; }
    int size() const { return _size; }
    T* get(int i) const { return _p[i]; }

    int add(T* val) {
        if (_size >= _allocSize) {
            resize();
        }
        _p[_size] = val;
        return _size++;
    }

    T* pop() {
        if (_size) {
            _size--;
            return _p[_size];
        } else {
            return 0;
        }
    }

    void clear() { _size = 0; }

private:
    void resize() {
        _allocSize = _allocSize * 2;
        T** oldP = _p;

        _p = (T**)malloc(sizeof(T*) * _allocSize);
        for (int i = 0; i < _size; ++i) {
            _p[i] = oldP[i];
        }

        free(oldP); oldP = 0;
    }

    T** _p;
    int _size;
    int _allocSize;
};


template <typename T>
class MValArray
{
public:
    MValArray()
            : _p(0),
              _size(0),
              _allocSize(0)
    {
    }

    void init(int initialSize) {
        _allocSize = initialSize;
        _p = (T*)malloc(sizeof(T) * initialSize);
    }

    ~MValArray() {
        free(_p); _p = 0;
    }

    T* data() const { return _p; }
    int size() const { return _size; }
    T& getRef(int i) const { return _p + i; }
    T* getPtr(int i) const { return _p + i; }
    T get(int i) const { return _p[i]; }

    int add(T val) {
        if (_size >= _allocSize) {
            resize();
        }
        _p[_size] = val;
        return _size++;
    }

    T* pop() {
        if (_size) {
            _size--;
            return _p + _size;
        } else {
            return 0;
        }
    }

    T* top() {
        if (_size) {
            return _p + (_size-1);
        } else {
            return 0;
        }
    }

    bool contains(T val) {
        for (int i = 0; i < _size; ++i) {
            if (_p[i] == val) {
                return true;
            }
        }
        return false;
    }

    void clear() { _size = 0; }

    void remove(int i) {
        if (i >= _size) {
            return;
        }
        int toMove = _size - i - 1;
        if (toMove > 0) {
            memcpy(_p + i, _p + i + 1, toMove * sizeof(T));
        }
        _size -= 1;
    }

private:
    void resize() {
        _allocSize = _allocSize * 2;
        T* oldP = _p;

        _p = (T*)malloc(sizeof(T) * _allocSize);
        for (int i = 0; i < _size; ++i) {
            _p[i] = oldP[i];
        }

        free(oldP); oldP = 0;
    }

    T* _p;
    int _size;
    int _allocSize;
};

class MDataReader
{
public:
    MDataReader() {
    }

    ~MDataReader() {
    }

    void init(u8* data, int size) {
        _data = data;
        _cur = data;
        _end = data + size;
    }

    bool done() {
        return _cur >= _end;
    }

    void skipToEnd() {
        _cur = _end;
    }

    void skipBytes(int bytes) {
        _cur += bytes;
    }

    i8 read8i() {
        if (_cur >= _end) {
            return 0;
        }

        i8 v = *(i8*)_cur;
        _cur += 1;
        return v;
    }

    u8 read8u() {
        if (_cur >= _end) {
            return 0;
        }

        u8 v = *(u8*)_cur;
        _cur += 1;
        return v;
    }

    u16 read16u() {
        if (_cur >= _end) {
            return 0;
        }

        u16 v = *(u16*)_cur;
        _cur += 2;
        return v;
    }

    i16 read16i() {
        if (_cur >= _end) {
            return 0;
        }

        i16 v = *(i16*)_cur;
        _cur += 2;
        return v;
    }

    u16 peek16u() {
        if (_cur >= _end) {
            return 0;
        }

        return *(u16*)_cur;
    }

    u8* getCurrentPos() {
        return _cur;
    }

    void setCurrentPos(u8* currentPos) {
        _cur = currentPos;
    }

    u8* getStartPos() {
        return _data;
    }

private:
    u8* _cur;
    u8* _data;
    u8* _end;
};

class MStrWriter
{
public:
    MStrWriter() :
            _allocSize(0),
            _size(0),
            _data(NULL) { }

    void init(int initialSize) {
        if (_allocSize != 0) {
            clear();
            resize(initialSize);
        } else {
            _allocSize = initialSize;
            _data = (char*)MMalloc(sizeof(char) * initialSize);
            _size = 1;
            _data[0] = 0;
        }
    }

    ~MStrWriter() {
        MFree(_data, _allocSize); _data = NULL;
    }

    char* data() const { return _data; }
    int size() const { return _size - 1; }

    int append(const char* str) {
        size_t len = MStrLen(str);
        if (_size) {
            int oldSize = _size;
            int newSize = _size + len;
            if (newSize >= _allocSize) {
                resize(newSize);
            }
            _size = newSize;
            memcpy(_data + (oldSize - 1), str, len);
            _data[_size - 1] = 0;
            return _size;
        } else {
            init(32);
            _size = len + 1;
            memcpy(_data, str, len);
            _data[len] = 0;
            return _size;
        }
    }

    int appendf(const char* str, ...);

    void clear() { _size = 1; _data[0] = 0; }

private:
    void resize(int newSize) {
        if (_allocSize >= newSize) {
            return;
        }
        int oldSize = _allocSize;
        while (_allocSize < newSize) {
            _allocSize = _allocSize * 2;
        }
        _data = (char*)MRealloc(_data, oldSize, _allocSize);
        _size = newSize;
    }

    char* _data;
    int _size;
    int _allocSize;
};

class MStrArray
{
public:
    MStrArray(int initialSize)
            : _p(0),
              _size(0),
              _allocSize(0)
    {
        _allocSize = initialSize;
        _p = (char**)malloc(sizeof(char*) * initialSize);
    }

    ~MStrArray() {
        for (int i = 0; i < _size; ++i) {
            free(_p[i]); _p[i] = 0;
        }
        free(_p); _p = 0;
    }

    char** data() const { return _p; }
    int size() const { return _size; }
    char* get(int i) const { return _p[i]; }

    int add(char* str) {
        if (_size >= _allocSize) {
            resize();
        }
        _p[_size] = str;
        return _size++;
    }

    int addCopy(char* str) {
        if (_size >= _allocSize) {
            resize();
        }
        size_t len = strlen(str);
        char* strCopy = (char*)malloc(len + 1);
        memcpy(strCopy, str, len + 1);
        _p[_size] = strCopy;
        return _size++;
    }

    char* pop() {
        if (_size) {
            _size--;
            return _p[_size];
        } else {
            return 0;
        }
    }

    void clear() {
        for (int i = 0; i < _size; ++i) {
            free(_p[i]); _p[i] = 0;
        }
        _size = 0;
    }

private:
    void resize() {
        _allocSize = _allocSize * 2;
        char** oldP = _p;

        _p = (char**)malloc(sizeof(char*) * _allocSize);
        for (int i = 0; i < _size; ++i) {
            _p[i] = oldP[i];
        }

        free(oldP); oldP = 0;
    }

    char** _p;
    int _size;
    int _allocSize;
};

typedef struct {
    u64 size;
    u32 exists;
    u64 lastModifiedTime;
} MFileInfo;

MFileInfo MGetFileInfo(const char* filePath);

#endif