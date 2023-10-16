/// AMM iSimulate Bridge
/// (c) 2023 University of Washington, CREST lab

#include <thread>
#include <string>
#include <vector>
#include <iostream>
#include <cmath>

#include <amm_std.h>
#include <signal.h>
#include "amm/BaseLogger.h"

#include <argp.h>

/// json library
#include "rapidjson/document.h"
#include "rapidjson/writer.h"

#include "websocket_session.hpp"

extern "C" {
   #include "service_discovery.h"
}

using namespace AMM;
using namespace std::chrono;
using namespace rapidjson;

// debug settings
bool debug_HF_data = false;

// declare DDSManager for this module
const std::string moduleName = "iSimulate Bridge";
const std::string configFile = "config/isimulate_bridge_amm.xml";
AMM::DDSManager<void>* mgr = new AMM::DDSManager<void>(configFile);
AMM::UUID m_uuid;
bool isRunning = true;

//std::mutex nds_mutex;
std::map<std::string, std::string> nodeDataStorage = {
      {"Cardiovascular_HeartRate", "0"},
      {"Cardiovascular_Arterial_Systolic_Pressure", "0"},
      {"Cardiovascular_Arterial_Diastolic_Pressure", "0"},
      {"BloodChemistry_Oxygen_Saturation", "0"},
      {"Respiration_EndTidalCarbonDioxide", "0"},
      {"Respiratory_Respiration_Rate", "0"},
      {"Energy_Core_Temperature", "0"}
   };

// initialize module state
float breathrate = 0;
float inflow;
bool isSimRunning = false;

// websocket session for asynchronous read/write to iSimulate device
net::io_context ioc;
auto ws_session = std::make_shared<websocket_session>(ioc);

std::string host = "";
std::string port = "";
const std::string target = "/";

struct arguments {
   bool render;
   bool phys;
   bool wave;
   bool tick;
   bool verbose;
} arguments;

// callback function for new data on websocket
void writeDataToMonitor() {
   std::string message =  "{\"type\": \"ChangeActionPacket\","
      "\"trendTime\": 0"
      ",\"hr\":" + nodeDataStorage["Cardiovascular_HeartRate"] +
      ",\"bpSys\":" + nodeDataStorage["Cardiovascular_Arterial_Systolic_Pressure"] +
      ",\"bpDia\":" + nodeDataStorage["Cardiovascular_Arterial_Diastolic_Pressure"] +
      ",\"spo2\":" + nodeDataStorage["BloodChemistry_Oxygen_Saturation"] +
      ",\"etco2\":" + nodeDataStorage["Respiration_EndTidalCarbonDioxide"] +
      ",\"respRate\":" + nodeDataStorage["Respiratory_Respiration_Rate"] +
      ",\"temp\":" + nodeDataStorage["Energy_Core_Temperature"] +
      ",\"cust1\":0,\"cust2\":0,\"cust3\":0,"
      "\"cvp\":10,\"cvpWaveform\":0,\"cvpVisible\":true,\"cvpAmplitude\": 2,\"cvpVariation\": 1,"
      "\"icp\":10,\"icpWaveform\":0,\"icpVisible\":true,\"icpAmplitude\": 2,\"icpVariation\": 1,"
      "\"icpLundbergAEnabled\": false,\"icpLundbergBEnabled\": false,"
      "\"papSys\":20,\"papDia\":10,\"papWaveform\": 0,\"papVisible\":true,\"papVariation\": 2,"
      "\"ecgWaveform\": 9,\"bpWaveform\": 0,\"spo2Waveform\": 0,\"etco2Waveform\":0,"
      "\"ecgVisible\": true,\"bpVisible\":true,\"spo2Visible\": true,"
      "\"etco2Visible\": true,\"rrVisible\": true,\"tempVisible\": true,"
      "\"custVisible1\": false,\"custVisible2\":false,\"custVisible3\":false,"
      "\"custLabel1\":\"\",\"custLabel2\":\"\",\"custLabel3\":\"\","
      "\"custMeasureLabel1\":\"\",\"custMeasureLabel2\":\"\",\"custMeasureLabel3\": \"\","
      "\"ectopicsPac\": 0,\"ectopicsPjc\": 0,\"ectopicsPvc\": 0,"
      "\"perfusion\":0,"
      "\"electricalInterference\":false,\"articInterference\":0,\"svvInterference\":0,\"sinusArrhythmiaInterference\": 1,"
      "\"ventilated\":false,"
      "\"electrodeStatus\": [true, true, true, true, true, true, true, true, true, true,true, true]"
      "}";
   LOG_DEBUG << "Writing message to iSimulate: {\"type\": \"ChangeActionPacket\" ...}";
   ws_session->do_write(message);
}

void onNewWebsocketMessage(const std::string body) {
   // parse web socket message as json data
   //std::string type, data, context;
   //Document document;
   //document.Parse(body.c_str());

   LOG_DEBUG << "iSimulate message: " << body ;
}

// init iSimulate device
void onWebsocketHandshake(const std::string body) {
   std::string message;

   // monitorState 3 = LifePak 15
   message =  "{\"type\": \"ChangeMonitorPacket\",\"monitorState\": 3}";
   LOG_DEBUG << "Writing message to iSimulate: " << message;
   ws_session->do_write(message);

   message =  "{\"type\": \"PowerOnPacket\"}";
   LOG_DEBUG << "Writing message to iSimulate: " << message;
   ws_session->do_write(message);

   message =  "{\"type\": \"VisibilityPacket\","
      "\"ecgVisible\": true,"
      "\"bpVisible\": true,"
      "\"spo2Visible\":true,"
      "\"etco2Visible\": true,"
      "\"rrVisible\": true,"
      "\"tempVisible\": true,"
      "\"custVisible1\": true,"
      "\"custVisible2\": true,"
      "\"custVisible3\": true,"
      "\"cvpVisible\": true,"
      "\"icpVisible\": true,"
      "\"papVisible\": true,"
      "}";
   LOG_DEBUG << "Writing message to iSimulate: " << message;
   ws_session->do_write(message);

   // requestedState 1 = running
   message =  "{\"type\": \"ScenarioChangeStatePacket\",\"requestedState\": 1}";
   LOG_DEBUG << "Writing message to iSimulate: " << message;
   ws_session->do_write(message);

   message =  "{\"type\": \"NibpPacket\",\"subType\": 0,\"bpSys\": 0,\"bpDia\": 0}";
   LOG_DEBUG << "Writing message to iSimulate: " << message;
   ws_session->do_write(message);

   writeDataToMonitor();
}

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
   //if ( arguments.verbose )
   //   LOG_DEBUG << "Tick received!";
}

void OnPhysiologyValue(AMM::PhysiologyValue& physiologyvalue, eprosima::fastrtps::SampleInfo_t* info){
   //const std::lock_guard<std::mutex> lock(nds_mutex);
   // store all received phys values
   if (!std::isnan(physiologyvalue.value())) {
      nodeDataStorage[physiologyvalue.name()] = std::to_string(physiologyvalue.value());
      //if ( arguments.verbose )
      //   LOG_DEBUG << "[AMM_Node_Data] " << physiologyvalue.name() << " = " << physiologyvalue.value();
      // phys values are updated every 200ms (5Hz)
      // forward to iSimulate device only once per data update
      // reduce frequency
      if (physiologyvalue.name()=="SIM_TIME") writeDataToMonitor();
   }

   static bool printRRdata = true;  // set flag to print only initial value received
   if (physiologyvalue.name()=="Respiratory_Respiration_Rate"){
      if ( arguments.verbose && printRRdata ) {
         LOG_DEBUG << "[AMM_Node_Data] Respiratory_Respiration_Rate" << "=" << physiologyvalue.value();
         printRRdata = false;
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
   std::cout << "Key pressed ... Shutting down." << std::endl;

   // Raise SIGTERM to trigger async signal handler
   std::raise(SIGTERM);
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
   ec.detach();

   // set up thread for service discovery
   LOG_INFO << "iSimulate device discovery";
   std::thread sd(service_discovery);
   sd.detach();

   //TODO: turn discovery into asynch process see mycroft bridge
   // block until iSimulate monitor has been discovered on the local network

   while (true) {
      if ( monitor_port !=0 && monitor_service_new) {
         LOG_INFO << "Monitor port aquired: " << monitor_port;
         LOG_INFO << "Monitor address aquired: " << monitor_address;
         host = monitor_address;
         port = std::to_string(monitor_port);
         monitor_service_new = false;
         break;
      }
      std::this_thread::sleep_for(milliseconds(20));
   }

   // set up websocket session
   ws_session->run(host, port, target);
   ws_session->registerHandshakeCallback(onWebsocketHandshake);
   ws_session->registerReadCallback(onNewWebsocketMessage);

   LOG_INFO << "iSimulate Bridge ready.";
   std::cout << "Listening for data... Press return to exit." << std::endl;

   // Capture SIGINT and SIGTERM to perform a clean shutdown
   net::signal_set signals(ioc, SIGINT, SIGTERM);
   signals.async_wait(
      [](boost::system::error_code const&, int signum)
      {
         LOG_WARNING << "Interrupt signal (" << signum << ") received.";
         // ask websocket session to close
         ws_session->do_close();
         // alternatively use ioc.stop() to stop the io_context. This will cause run() to return immediately
      });

   // Run the I/O context.
   // The call will return when the socket is closed.
   ioc.run();

   mgr->Shutdown();
   std::this_thread::sleep_for(milliseconds(100));
   delete mgr;

   LOG_INFO << "iSimulate Bridge shutdown.";
   return EXIT_SUCCESS;
}
