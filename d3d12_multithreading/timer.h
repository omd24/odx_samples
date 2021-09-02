#pragma once

struct Timer {
private:
    // -- source time data uses QPC units
    LARGE_INTEGER qpc_frequency_;
    LARGE_INTEGER qpc_last_time_;
    UINT64 qpc_max_delta_;

    // -- derived time data uses canonical tick format
    UINT64 elapsed_ticks_;
    UINT64 total_ticks_;
    UINT64 left_over_ticks_;

    // -- data to framerate
    UINT32 frame_count_;
    UINT32 frames_per_second_;
    UINT32 frames_this_second_;
    UINT64 qpc_second_counter_;

    // -- data to configure fixed timestep mode
    bool is_fixed_timestep_;
    UINT64 target_elapsed_ticks_;
public:
    // -- 10,000,000 ticks per second integer format
    static constexpr UINT64 TicksPerSecond = 10'000'000;

    static double Ticks2Seconds (UINT64 ticks) {
        return static_cast<double>(ticks) / TicksPerSecond;
    }
    static UINT64 Seconds2Ticks (double seconds) {
        return static_cast<UINT64>(seconds * TicksPerSecond);
    }

    Timer () :
        elapsed_ticks_(0), total_ticks_(0), left_over_ticks_(0),
        frame_count_(0), frames_per_second_(0), frames_this_second_(0),
        qpc_second_counter_(0), is_fixed_timestep_(false),
        target_elapsed_ticks_(TicksPerSecond / 60) {

        QueryPerformanceFrequency(&qpc_frequency_);
        QueryPerformanceCounter(&qpc_last_time_);

        // -- init max_delta to 1/10 of a second
        qpc_max_delta_ = qpc_frequency_.QuadPart / 10;
    }

    // -- total time since the start of program
    UINT64 GetTotalTicks () const {
        return total_ticks_;
    }
    double GetTotalSeconds () const {
        return Ticks2Seconds(total_ticks_);
    }

    // -- elapsed time since previous Update call
    UINT64 GetElapsedTicks () const {
        return elapsed_ticks_;
    }
    double GetElapsedSeconds () const {
        return Ticks2Seconds(elapsed_ticks_);
    }

    // -- total number of updates since start of the program
    UINT32 GetFrameCount () const { return frame_count_; }

    // -- current frame rate
    UINT32 GetFramesPerSecond () const { return frames_per_second_; }

    // -- use fixed or variable timestep mode ?!
    void SetFixedTimeStep (bool is_fixed_step) {
        is_fixed_timestep_ = is_fixed_step;
    }

    // -- how often to call Update (in fixed timestep mode)
    void SetTargetElaspedTicks (UINT64 target_elapsed) {
        target_elapsed_ticks_ = target_elapsed;
    }
    void SetTargetElapsedSeconds (double target_elapsed) {
        target_elapsed_ticks_ = Seconds2Ticks(target_elapsed);
    }

    // -- after any intentional discontiniuty (e.g., a blocking IO)
    // -- reset elasped time to avoid forcing fixed-timestep logic
    // -- catching up to Update calls
    void ResetElaspedTime () {
        QueryPerformanceCounter(&qpc_last_time_);
        left_over_ticks_ = 0;
        frames_per_second_ = 0;
        frames_this_second_ = 0;
        qpc_second_counter_ = 0;
    }

    // -- fptr to call when updating
    typedef void (*LPUPDATEFUNC) (void);

    // -- update timer while calling the specified Update fptr
    // -- (calling Update as many times as necessary)
    void Tick (LPUPDATEFUNC update_fptr = nullptr) {
        // -- query current time
        LARGE_INTEGER current_time;
        QueryPerformanceCounter(&current_time);
        UINT64 delta_time = current_time.QuadPart - qpc_last_time_.QuadPart;

        qpc_last_time_ = current_time;
        qpc_second_counter_ += delta_time;

        // -- clamp large deltas (e.g., after a long pause)
        if (delta_time > qpc_max_delta_)
            delta_time = qpc_max_delta_;

        // -- convert qpc unit to canonical tick format
        delta_time *= TicksPerSecond;
        delta_time /= qpc_frequency_.QuadPart;

        UINT32 last_frame_count = frame_count_;
        if (is_fixed_timestep_) {
            // -- if app is running close to target (within 1/4 of ms)
            // -- just clamp clock to match target value
            if (
                abs(static_cast<int>(delta_time - target_elapsed_ticks_)) <
                TicksPerSecond / 4000
            ) {
                delta_time = target_elapsed_ticks_;
            }

            left_over_ticks_ += delta_time;

            while (left_over_ticks_ >= target_elapsed_ticks_) {
                elapsed_ticks_ = target_elapsed_ticks_;
                total_ticks_ += target_elapsed_ticks_;
                left_over_ticks_ -= target_elapsed_ticks_;
                ++frame_count_;
                if (update_fptr)
                    update_fptr();
            }
        } else {    // -- variable timestep update logic
            elapsed_ticks_ = delta_time;
            total_ticks_ += delta_time;
            left_over_ticks_ = 0;
            ++frame_count_;
            if (update_fptr)
                update_fptr();
        }

        // -- update framerate data
        if (frame_count_ != last_frame_count)
            ++frames_this_second_;
        if (
            qpc_second_counter_ >=
            static_cast<UINT64>(qpc_frequency_.QuadPart)
        ) {
            frames_per_second_ = frames_this_second_;
            frames_this_second_ = 0;
            qpc_second_counter_ %= qpc_frequency_.QuadPart;
        }

    }
};

