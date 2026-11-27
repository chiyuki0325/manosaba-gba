#ifndef DIALOGUE_PLAYBACK_H
#define DIALOGUE_PLAYBACK_H

namespace manosaba
{
enum class dialogue_playback_mode
{
    normal = 0,
    fast = 48,
    automatic = 96
};

class dialogue_playback
{
  public:
    void reset()
    {
        *this = {};
    }
    void update(bool dialogue, bool pressed, bool released, int frames)
    {
        if (_automatic)
        {
            if (pressed)
                reset();
            return;
        }
        if (!dialogue || frames >= _remaining)
            _remaining = _edges = 0;
        else
            _remaining -= frames;
        if (!dialogue)
            return;
        if (!_edges)
        {
            if (pressed)
            {
                _remaining = double_tap_frames + 1; // Include a release on the deadline.
                _edges = 1;
            }
            return;
        }
        if (((_edges & 1) ? released : pressed) && ++_edges == 4)
        {
            _remaining = _edges = 0;
            _automatic = true;
        }
    }
    dialogue_playback_mode mode(bool held, bool dialogue) const
    {
        if (held)
            return dialogue_playback_mode::fast;
        return _automatic && dialogue ? dialogue_playback_mode::automatic : dialogue_playback_mode::normal;
    }

  private:
    static constexpr int double_tap_frames = 48;
    int _remaining = 0, _edges = 0;
    bool _automatic = false;
};
} // namespace manosaba
#endif
