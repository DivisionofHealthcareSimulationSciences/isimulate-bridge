/// AMM iSimulate Bridge
/// (c) 2023 University of Washington, CREST lab

#include <thread>
#include <string>
#include <vector>
#include <iostream>

#include <amm_std.h>
#include <signal.h>
#include "amm/BaseLogger.h"

#include <argp.h>

extern "C" {
   #include "service_discovery.h"
}

using namespace AMM;
using namespace std;
using namespace std::chrono;

// debug settings
bool debug_HF_data = false;

// declare DDSManager for this module
const std::string moduleName = "iSimulate Bridge";
const std::string configFile = "config/isimulate_bridge_amm.xml";
AMM::DDSManager<void>* mgr = new AMM::DDSManager<void>(configFile);
AMM::UUID m_uuid;
bool isRunning = true;

// initialize module state
float breathrate = 0;
float inflow;
bool isSimRunning = false;

struct arguments {
   bool render;
   bool phys;
   bool wave;
   bool tick;
   bool verbose;
} arguments;

void OnNewSimulationControl(AMM::SimulationControl& simControl, eprosima::fastrtps::SampleInfo_t* info) {

   switch (simControl.type()) {

   case AMM::ControlType::RUN :

      LOG_INFO << "SimControl Message recieved; Run sim.";
      isSimRunning = true;
      break;

   case AMM::ControlType::HALT :

      LOG_INFO << "SimControl Message recieved; Halt sim.";
      isSimRunning = false;
      break;

   case AMM::ControlType::RESET :

      LOG_INFO << "SimControl Message recieved; Reset sim.";

      isSimRunning = false;
      break;

   case AMM::ControlType::SAVE :

      LOG_INFO << "SimControl Message recieved; Save sim.";
      //mgr->WriteModuleConfiguration(currentState.mc);
      break;
   }
}

void OnNewTick(AMM::Tick& tick, eprosima::fastrtps::SampleInfo_t* info) {
   //LOG_DEBUG << "Tick received!";
   if (arguments.tick) {
      auto currentTime = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
      auto source_time = int64_t(info->sourceTimestamp.seconds())*1000 + int64_t(info->sourceTimestamp.fraction())*1000/ULONG_MAX;
      LOG_DEBUG << "Sim tick lag: " << currentTime - source_time << " ms";
   }
}

void OnPhysiologyValue(AMM::PhysiologyValue& physiologyvalue, eprosima::fastrtps::SampleInfo_t* info){

   static bool printRRdata = true;  // set flag to print only initial value received
   if (physiologyvalue.name()=="Respiratory_Respiration_Rate"){
      if ( arguments.verbose && printRRdata ) {
         LOG_DEBUG << "[AMM_Node_Data] Respiratory_Respiration_Rate" << "=" << physiologyvalue.value();
         printRRdata = false;
      }
      breathrate = physiologyvalue.value();
      if (arguments.phys) {
         auto currentTime = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
         auto source_time = int64_t(info->sourceTimestamp.seconds())*1000 + int64_t(info->sourceTimestamp.fraction())*1000/ULONG_MAX;
         LOG_DEBUG << "Phys value lag (Respiratory_Respiration_Rate): " << currentTime - source_time << " ms";
      }
   }
}

void OnPhysiologyWaveform(AMM::PhysiologyWaveform &waveform, SampleInfo_t *info) {
   static int printHFdata = 10;   // initialize counter to print first xx high freequency data points
   if ( arguments.verbose && printHFdata > 0) {
      LOG_DEBUG << "[AMM_Node_Data](HF) " << waveform.name() << "=" << waveform.value();
      printHFdata -= 1;
   }

   if (waveform.name()=="Respiratory_Inspiratory_Flow"){
      inflow=waveform.value();
      if (arguments.wave) {
         auto currentTime = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
         auto source_time = int64_t(info->sourceTimestamp.seconds())*1000 + int64_t(info->sourceTimestamp.fraction())*1000/ULONG_MAX;
         LOG_DEBUG << "Physiology waveform lag (Respiratory_Inspiratory_Flow): " << currentTime - source_time << " ms";
      }
   }
   if (debug_HF_data) {
      LOG_DEBUG << "[AMM_Node_Data](HF)" << waveform.name() << "=" << waveform.value();
   }
}

void OnNewRenderModification(AMM::RenderModification &rendMod, SampleInfo_t *info) {
   auto currentTime = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
   auto source_time = int64_t(info->sourceTimestamp.seconds())*1000 + int64_t(info->sourceTimestamp.fraction())*1000/ULONG_MAX;

   if (rendMod.data().to_string().find("START_OF_INHALE") != std::string::npos) {
      //LOG_DEBUG << "Render Modification received: START_OF_INHALE";
      if (arguments.render) {
         LOG_DEBUG << "Render mod lag (START_OF_INHALE): " << currentTime - source_time << " ms";
      }
   } else if (rendMod.data().to_string().find("START_OF_EXHALE") != std::string::npos) {
      //LOG_DEBUG << "Render Modification received: START_OF_EXHALE";
      if ( arguments.render ) {
         LOG_DEBUG << "Render mod lag (START_OF_EXHALE): " << currentTime - source_time << " ms";
      }
   } else {
      //LOG_DEBUG << "Render Modification received:\n"
      //          << "Type:      " << rendMod.type() << "\n"
      //          << "Data:      " << rendMod.data();
   }
}

void PublishOperationalDescription() {
    AMM::OperationalDescription od;
    od.name(moduleName);
    od.model("iSimulate Bridge");
    od.manufacturer("CREST");
    od.serial_number("0000");
    od.module_id(m_uuid);
    od.module_version("1.2.0");
    od.description("A bridge module to connect MoHSES to an iSimulate patient monitor.");
    const std::string capabilities = AMM::Utility::read_file_to_string("config/isimulate_bridge_capabilities.xml");
    od.capabilities_schema(capabilities);
    od.description();
    mgr->WriteOperationalDescription(od);
}

void PublishConfiguration() {
    AMM::ModuleConfiguration mc;
    auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    mc.timestamp(ms);
    mc.module_id(m_uuid);
    mc.name(moduleName);
    const std::string configuration = AMM::Utility::read_file_to_string("config/isimulate_bridge_configuration.xml");
    mc.capabilities_configuration(configuration);
    mgr->WriteModuleConfiguration(mc);
}

void checkForExit() {
   // wait for key press
   std::cin.get();
   isRunning = false;
   std::cout << "Key pressed ... Shutting down." << std::endl;
}

void signalHandler(int signum) {
   LOG_WARNING << "Interrupt signal (" << signum << ") received.";

   if (signum == 15) {
      mgr->Shutdown();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      delete mgr;
      LOG_INFO << "Shutdown complete";
   }
   exit(signum);
}

// set up command line option checking using argp.h
const char *argp_program_version = "mohses_isimulate_bridge v1.2.0";
const char *argp_program_bug_address = "<rainer@uw.edu>";
static char doc[] = "Bridge module for data exchange between MoHSES and iSimulate device.";
static char args_doc[] = "";
static struct argp_option options[] = {
    { "render", 'r', 0, 0, "Show render mod latency"},
    { "phys", 'p', 0, 0, "Show physiology value latency"},
    { "wave", 'w', 0, 0, "Show physiology waveform latency"},
    { "tick", 't', 0, 0, "Show sim tick latency"},
    { "verbose", 'v', 0, 0, "Print extra data"},
    { 0 }
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
   struct arguments *arguments = (struct arguments*)(state->input);
   int vol;
   switch (key) {
      case 'r':
         arguments->render = true;
         break;
      case 'p':
         arguments->phys = true;
         break;
      case 'w':
         arguments->wave = true;
         break;
      case 't':
         arguments->tick = true;
         break;
      case 'v':
         arguments->verbose = true;
         break;
      case ARGP_KEY_ARG: return 0;
      default: return ARGP_ERR_UNKNOWN;
   }
   return 0;
}

static struct argp argp = { options, parse_opt, args_doc, doc, 0, 0, 0 };

int main(int argc, char *argv[]) {

   static plog::ColorConsoleAppender<plog::TxtFormatter> consoleAppender;
   plog::init(plog::verbose, &consoleAppender);

   // process command line options
   arguments.render = false;
   arguments.phys = false;
   arguments.wave = false;
   arguments.tick = false;
   arguments.verbose = false;

   argp_parse(&argp, argc, argv, 0, 0, &arguments);

   LOG_INFO << "=== [ iSimulate Bridge ] ===";

   mgr->InitializeOperationalDescription();
   mgr->CreateOperationalDescriptionPublisher();

   mgr->InitializeModuleConfiguration();
   mgr->CreateModuleConfigurationPublisher();

   mgr->InitializeSimulationControl();
   mgr->CreateSimulationControlSubscriber(&OnNewSimulationControl);

   mgr->InitializeStatus();
   mgr->CreateStatusPublisher();

   mgr->InitializeTick();
   mgr->CreateTickSubscriber(&OnNewTick);

   mgr->InitializePhysiologyValue();
   mgr->CreatePhysiologyValueSubscriber(&OnPhysiologyValue);

   mgr->InitializePhysiologyWaveform();
   mgr->CreatePhysiologyWaveformSubscriber(&OnPhysiologyWaveform);

   mgr->InitializeRenderModification();
   mgr->CreateRenderModificationSubscriber(&OnNewRenderModification);

   m_uuid.id(mgr->GenerateUuidString());

   std::this_thread::sleep_for(milliseconds(250));

   PublishOperationalDescription();
   PublishConfiguration();

   // set up thread to check console for "exit" command
   std::thread ec(checkForExit);

   // set up thread for service discovery
   std::thread sd(service_discovery);
   sd.detach();

   // set up signal handler for termination by supervisor or similar process manager
   signal(SIGINT, signalHandler);
   signal(SIGTERM, signalHandler);

   LOG_INFO << "iSimulate Bridge ready.";
   std::cout << "Listening for data... Press return to exit." << std::endl;

   while ( isRunning ) {
      if ( monitor_port !=0 && monitor_service_new) {
         LOG_INFO << "Monitor port aquired: " << monitor_port;
         LOG_INFO << "Monitor address aquired: " << monitor_address;
         monitor_service_new = false;
      }
      std::this_thread::sleep_for(milliseconds(20));
   }

   ec.join();

   mgr->Shutdown();
   std::this_thread::sleep_for(milliseconds(100));
   delete mgr;

   LOG_INFO << "iSimulate Bridge shutdown.";
}
