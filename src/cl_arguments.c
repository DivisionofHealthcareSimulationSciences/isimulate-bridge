
#include <argp.h>

struct arguments {
   int monitor;
   bool verbose;
} arguments;

// set up command line option checking using argp.h
const char *argp_program_version = "mohses_isimulate_bridge v1.2.0";
const char *argp_program_bug_address = "<rainer@uw.edu>";

static char doc[] = 
    "Bridge module for data exchange between MoHSES and iSimulate device.\
    \vMonitor IDs\n\
    1 - GE Carescape                    17 - Zoll R-Series\n\
    2 - Zoll X-Series                   18 - Capnostream 35\n\
    3 - LifePak 15                      19 - Generic Monitor\n\
    4 - Corpuls 3                       20 - Generic Defibrillator\n\
    5 - Connex Vital Signs Monitor      21 - Basic Numerics\n\
    6 - LifePak 1000                    22 - Basic Wave\n\
    7 - Corpuls1                        23 - Generic AED\n\
    9 - Corpuls AED                     24 - ReVel Ventilator\n\
    10 - Propaq MD                      25 - Schiller DEFIGARD Touch 7\n\
    13 - HeartStart MRx                 26 - Schiller PHYSIOGARD Touch 7\n\
    14 - HeartStart MRx for Hospital    27 - Zoll EMV+\n\
    15 - MX800                          28 - Corpuls3 4th Generation\n\
    16 - LifePak 20";

static char args_doc[] = "";
static struct argp_option options[] = {
    { "monitor",  'm', "MONITOR", 0, "Select monitor model by ID"},
    { "verbose",  'v', 0, 0, "Print extra data"},
    { 0 }
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
   struct arguments *arguments = (struct arguments*)(state->input);

   switch (key) {
      case 'm':
         char *out;
         arguments->monitor = strtol(arg, &out, 10);
         if (*out) {
            argp_usage (state);
            return ARGP_ERR_UNKNOWN;
         }
         break;
      case 'v':
         arguments->verbose = true;
         break;
      case ARGP_KEY_ARG: 
         argp_usage (state);
         break;
      default: 
         return ARGP_ERR_UNKNOWN;
   }
   return 0;
}

static struct argp argp = { options, parse_opt, args_doc, doc};
