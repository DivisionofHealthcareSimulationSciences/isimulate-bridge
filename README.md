# iSimulate Bridge

AMM/MoHSES module for connecting to an iSimulate monitor on the local network.

## Dependencies

The iSimulate Bridge requires the [AMM Standard Library](https://github.com/AdvancedModularManikin/amm-library) be built and available (see AMM lib dependencies).
The iSimulate Bridge module also requires:

- avahi-client
- avahi-common

`$ sudo apt install libavahi-client-dev`

## Installation

```bash
    $ git clone https://github.com/DivisionofHealthcareSimulationSciences/isimulate-bridge.git
    $ cd isimulate-bridge
    $ mkdir build && cd build
    $ cmake ..
    $ cmake --build . --target install
```

## Contact
Contact Rainer Leuschke (rainer@uw.edu) with any questions.
