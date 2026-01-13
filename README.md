# Move Anything Space Delay

RE-201 style tape delay audio effect module for Move Anything.

## Features

- **Time**: Delay time from 50ms to 800ms
- **Feedback**: Echo repeats with gentle saturation
- **Mix**: Dry/wet blend
- **Tone**: High-frequency rolloff on repeats (tape simulation)
- **Flutter**: Tape wow/flutter pitch modulation

## Building

```bash
./scripts/build.sh      # Build for ARM64 via Docker
./scripts/install.sh    # Deploy to Move
```

## Signal Flow

```
Input ---+-------------------------------- Dry ----+
         |                                         |
         +---> Delay Line ---> Tone Filter --> Wet-+---> Mix ---> Output
                   ^               |               |
                   |               v               |
                   +--- Saturation <-- Feedback <--+
```

## Installation

The module installs to `/data/UserData/move-anything/modules/chain/audio_fx/spacecho/`

## Credits

Based on [TapeDelay](https://github.com/cyrusasfa/TapeDelay) by Cyrus Afsary.

Inspired by the [Roland RE-201 Space Echo](https://www.roland.com/global/promos/space_echo_history/) tape delay unit (1974).

## License

MIT License - Copyright (c) 2025 Charles Vestal
