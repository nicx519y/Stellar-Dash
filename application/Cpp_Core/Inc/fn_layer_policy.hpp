#ifndef FN_LAYER_POLICY_HPP
#define FN_LAYER_POLICY_HPP

struct FnLayerDecision {
    bool processHotkeys;
    bool submitNeutral;
    bool submitNormal;
};

class FnLayerPolicy {
public:
    FnLayerDecision update(bool fnPressed, bool fnWasPressed)
    {
        if (fnPressed && !fnWasPressed) {
            neutralPending_ = true;
        } else if (!fnPressed) {
            neutralPending_ = false;
        }

        return {
            fnPressed || fnWasPressed,
            fnPressed && neutralPending_,
            !fnPressed,
        };
    }

    void onNeutralSubmitted(bool submitted)
    {
        if (submitted) {
            neutralPending_ = false;
        }
    }

    void reset() { neutralPending_ = false; }
    bool isNeutralPending() const { return neutralPending_; }

private:
    bool neutralPending_ = false;
};

#endif
