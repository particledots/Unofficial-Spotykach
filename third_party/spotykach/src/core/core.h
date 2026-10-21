// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include <array>
#include <random>

#include "nocopy.h"
#include "config.h"

#include "driver.h"
#include "modulator.h"
#include "xfade.h"
#include "panner.h"
#include "smooth.h"

namespace spotykach {

enum class Route: uint8_t {
  DoubleMono = 1,
  Stereo,
  GenerativeStereo
};

enum class ModType: uint8_t {
  Follow,
  LFO
};

class Core {
public:
  Core();
  ~Core() {}
  
  // MODIFIED for Unofficial Spotykach (2026): buffers are supplied by the caller.
  struct Buffers {
    Buffer::Frame* main[Deck::Count];
    size_t main_size;
    float* detect[Deck::Count][2];
    float* delay[Deck::Count][2];
    size_t* slices[Deck::Count];
    Event* track[Deck::Count];
  };
  void init(const float sample_rate, const float buffer_size, const Buffers& buffers);

  Driver& driver() { return _driver; }
  Panner& panner() { return _panner; }
  Deck& deck(const Deck::Ref ref) { return _decks[ref]; }
  Modulator& mod(const Deck::Ref ref) { return _mod[ref]; }

  Deck::Source source(const Deck::Ref deck) const { return _source[deck]; }
  void set_source(const Deck::Source source, const Deck::Ref deck) { _source[deck] = source; }
  void infer_panner_mode();

  float mix() const { return _xfade.Stage(); }
  void set_mix(const float value) { _mix = value; }
  void mix_mod_in(const float value) { 
    if (value < 0.01) _mix_mod = 0.f;
    else _mix_mod = value; 
  }

  void set_route(const Route val);
  Route route() const { return _route; }

  void set_click_mix(const float value) { _click_mix = value; }

  // MODIFIED for Unofficial Spotykach (2026): per-deck input and output trims (linear gain, smoothed per
  // sample inside process()). The hardware has analog trimmers here; the firmware has no equivalent.
  void set_trim_in(const Deck::Ref deck, const float gain) { _trim_in[deck] = gain; }
  void set_trim_out(const Deck::Ref deck, const float gain) { _trim_out[deck] = gain; }
  
  void prepare();
  void process(const float* const* in, float** out, size_t size);

private:
  NOCOPY(Core)
  
  void _resolve_mix();

  std::array<Deck, Deck::Count> _decks;
  std::array<Modulator, Deck::Count> _mod;
  Driver  _driver;
  XFade   _xfade;
  Click   _click;
  Panner  _panner;
  OnePoleSmoother _mix_smooth;

  std::array<float, Deck::Count> _pos_mod_amnt;
  std::array<Deck::Source, Deck::Count> _source;

  Route _route;

  std::array<float, 2> _bus;

  float _click_mix;
  float _mix;
  float _mix_mod;

  // MODIFIED for Unofficial Spotykach (2026): trims (see set_trim_in / set_trim_out).
  float _trim_in[Deck::Count] = { 1.f, 1.f };
  float _trim_out[Deck::Count] = { 1.f, 1.f };
  float _trim_in_now[Deck::Count] = { 1.f, 1.f };
  float _trim_out_now[Deck::Count] = { 1.f, 1.f };
};

};
