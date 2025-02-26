#ifndef __BASE_STATE_H__
#define __BASE_STATE_H__

class BaseState {
    public:
        virtual void setup() = 0;
        virtual void loop() = 0;
        virtual void reset() = 0;
};

#endif