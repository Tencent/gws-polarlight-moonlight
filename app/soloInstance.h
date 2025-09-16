// Copy from Github XLang Project

#pragma once
template <class T>
class SoloInstance {
public:
    inline static T& I() {
        static T _instance;
        return _instance;
    }

protected:
    SoloInstance(void) {}
    virtual ~SoloInstance(void) {}
    SoloInstance(const SoloInstance<T>&);
    SoloInstance<T>& operator= (const SoloInstance<T> &);
};
