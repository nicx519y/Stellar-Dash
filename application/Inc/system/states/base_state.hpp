#ifndef __BASE_STATE_H__
#define __BASE_STATE_H__

class BaseState {
    public:
        virtual ~BaseState() = default;
        virtual bool enter() = 0;
        virtual void tick() = 0;
        virtual void exit() = 0;
};

#endif
