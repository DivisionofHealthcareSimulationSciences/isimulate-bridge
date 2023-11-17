/// AMM iSimulate Bridge
/// (c) 2023 University of Washington, CREST lab

#include <thread>
#include <string>
#include <vector>
#include <iostream>
#include <cmath>
#include <sstream>

#include <amm_std.h>
#include <signal.h>
#include "amm/BaseLogger.h"

/// json library
#include "rapidjson/document.h"
#include "rapidjson/writer.h"

#include "websocket_session.hpp"

extern "C" {
   #include "service_discovery.h"
   #include "cl_arguments.c"
}

using namespace AMM;
using namespace std::chrono;
using namespace rapidjson;

// declare DDSManager for this module
const std::string moduleName = "iSimulate Bridge";
const std::string configFile = "config/isimulate_bridge_amm.xml";
AMM::DDSManager<void>* mgr = new AMM::DDSManager<void>(configFile);
AMM::UUID m_uuid;

//std::mutex nds_mutex;
std::map<std::string, std::string> nodeDataStorage = {
      {"Cardiovascular_HeartRate", "0"},
      {"Cardiovascular_Arterial_Systolic_Pressure", "0"},
      {"Cardiovascular_Arterial_Diastolic_Pressure", "0"},
      {"BloodChemistry_Oxygen_Saturation", "0"},
      {"Respiration_EndTidalCarbonDioxide", "0"},
      {"Respiratory_Respiration_Rate", "0"},
      {"Energy_Core_Temperature", "0"},
      {"SIM_TIME", "0"},
   };

// init monitor waveforms
int ecgWaveform = 9;       // 9 -> Sinus
int bpWaveform = 0;        // 0 -> Normal
int spo2Waveform = 0;      // 0 -> Normal
int etco2Waveform = 0;     // 0 -> Normal

// initialize module state
int sim_status = 0;  // 0 - initial/reset, 1 - running, 2 - paused
int64_t lastTick = 0;

// websocket session for asynchronous read/write to iSimulate device
net::io_context ioc;
auto ws_session = std::make_shared<websocket_session>(ioc); 
bool try_reconnect = true;
bool websocket_connected = false;
bool monitor_initialized = false;

std::string host = "";
std::string port = "";
const std::string target = "/";

//write data packets to websocket
void writeConnectionTypePacket(int con) {
   std::string message = "{\"type\":\"ConnectionTypePacket\",\"connectionType\":" + std::to_string(con) + "}";
   // iSimulate monitor should respond with settings request and scenario request
   LOG_DEBUG << "Writing message to iSimulate: " << message;
   ws_session->do_write(message);
}

void writeSettingsPacket() {
   std::string message =  "{\"type\": \"SettingsPacket\""
      ",\"tempMeasureF\":true"
      ",\"etco2MeasurekPa\":false"
      ",\"cprDepthMeasureInch\":false"
      ",\"pacingThreshold\":50"
      ",\"preserveCO2\":true"
      ",\"seeThruCPR\":true"
      ",\"pacerCapture\":true"
      ",\"monitorControlsVolume\":true"
      ",\"nibpMeasure\":0,\"weightMeasure\":0,\"ibpMeasure\":0}";
   LOG_DEBUG << "Writing message to iSimulate: " << message;
   ws_session->do_write(message);
}

void writeScenarioPacket() {
   std::string message = "{\"type\": \"ScenarioCurrentStatePacket\""
      ",\"scenarioData\": {\"scenarioId\": \"\""
                           ",\"scenarioType\": \"Vital Signs\""
                           ",\"scenarioName\": \"\""
                           ",\"scenarioTime\": 600"
                           ",\"scenarioMonitorType\": " + std::to_string(arguments.monitor) +
                           ",\"scenarioStory\": {\"history\": \"\""
                                                ",\"course\": \"\""
                                                ",\"discussion\": \"\"}"
                           ",\"patientInformation\": {\"patientName\": \"\""
                                                      ",\"patientSex\":0"
                                                      ",\"patientCondition\": \"\""
                                                      ",\"patientAdmitted\": 0"
                                                      ",\"patientAge\": 30"
                                                      ",\"patientPhotoId\":0}}"
      ",\"scenarioState\": 0"
      ",\"studentInfo\": {\"studentName\": \"\""
                        ",\"studentNumber\": \"\""
                        ",\"studentEmail\": \"\"}}";
   LOG_DEBUG << "Writing message to iSimulate: " << message;
   ws_session->do_write(message);
}

void writeChangeActionPacket() {
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
      "\"ecgWaveform\": " + std::to_string(ecgWaveform) +
      ",\"bpWaveform\": " + std::to_string(bpWaveform) +
      ",\"spo2Waveform\": " + std::to_string(spo2Waveform) +
      ",\"etco2Waveform\": " + std::to_string(etco2Waveform) +
      ",\"ecgVisible\": true,\"bpVisible\":true,\"spo2Visible\": true,"
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
   if ( arguments.verbose )
      LOG_DEBUG << "Writing message to iSimulate: " << message;
   // else 
   //   LOG_DEBUG << "Writing message to iSimulate: {\"type\": \"ChangeActionPacket\" ...}";
   ws_session->do_write(message);
}

void writeSyncTimesPacket() {
   std::string message =  "{\"type\": \"SyncTimesPacket\""
      ",\"actualTime\":0"
      ",\"virtualTime\":" + nodeDataStorage["SIM_TIME"] +
      ",\"alarmTime\":0,\"isVirtualTimePaused\":false}";
   LOG_DEBUG << "Writing message to iSimulate: " << message; //{\"type\": \"SyncTimesPacket\" ...}";
   ws_session->do_write(message);
}

void writeScenarioChangeStatePacket(int state) {
   // requestedState values: 0 - initial, 1 - running, 2 - paused, 3 - finished
   std::string message =  "{\"type\":\"ScenarioChangeStatePacket\",\"requestedState\":" + std::to_string(state) + "}";
   LOG_DEBUG << "Writing message to iSimulate: " << message;
   ws_session->do_write(message);
}

void writePowerOnPacket() {
   std::string message =  "{\"type\":\"PowerOnPacket\"}";
   LOG_DEBUG << "Writing message to iSimulate: " << message;
   ws_session->do_write(message);
}

void writeVisibilityPacket() {
   std::string message =  "{\"type\": \"VisibilityPacket\","
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
}

void writeNibpPacket() {
   std::string message =  "{\"type\": \"NibpPacket\",\"subType\": 0,\"bpSys\": 0,\"bpDia\": 0}";
   LOG_DEBUG << "Writing message to iSimulate: " << message;
   ws_session->do_write(message);
}

void writeChangeMonitorPacket() {
   std::string message =  "{\"type\": \"ChangeMonitorPacket\""
      ",\"monitorState\":" + std::to_string(arguments.monitor) + "}";
   LOG_DEBUG << "Writing message to iSimulate: " << message;
   ws_session->do_write(message);
}

void writeDisconnectPackage() {
   std::string message =  "{\"type\":\"DisconnectPacket\"}";
   LOG_DEBUG << "Writing message to iSimulate: " << message;
   ws_session->do_write(message);
}

// callback function for new data on websocket
void onNewWebsocketMessage(const std::string body) {
   // parse web socket message as json data
   std::string type;
   Document document;
   document.Parse(body.c_str());

   if (document.HasMember("type") && document["type"].IsString()) {
      type = document["type"].GetString();
      if (type.compare("DebriefPacket") == 0) {
         // ignore debrief, only log message type
         LOG_DEBUG << "iSimulate message: {\"type\": \"DebriefPacket\", ...}";
         return;
      }
      LOG_DEBUG << "iSimulate message: " << body ;
      if (type.compare("SettingsRequestPacket") == 0) {
         writeSettingsPacket();
      } else if (type.compare("ScenarioRequestPacket") == 0) {
         // respond to request
         writeScenarioPacket();
         // then fire up monitor
         writeSyncTimesPacket();
         writePowerOnPacket();
         writeScenarioChangeStatePacket(sim_status);
         writeVisibilityPacket();
         monitor_initialized = true;
         //writeNibpPacket();
         //writeChangeActionPacket();
      } else if (type.compare("ScenarioCurrentStatePacket") == 0) {
         // monitor sends this if it has already been initialized (and running or paused) when connection is established
         // bridge module cannot distinguish between paused and reset/init state of sim on startup
         // when sim is not running and monitor is paused assume the sim is paused
         if (document.HasMember("scenarioState") && document["scenarioState"].IsInt()) {
            int scenarioState = document["scenarioState"].GetInt();
            if (scenarioState == 2 && sim_status == 0 ) writeScenarioChangeStatePacket(2);
            else writeScenarioChangeStatePacket(sim_status);
            monitor_initialized = true;
         }
      } else if (type.compare("DisconnectPacket") == 0) {
         // monitor is closing websocket connection
         // pending async_read returns with eof
         // which should result in io context running out of work, returning and connection being reset
         // ioc.stop();
      }
   } else {
      LOG_ERROR << "iSimulate message (no type): " << body ;
   }
}

// init iSimulate device
void onWebsocketHandshake(const std::string body) {
   websocket_connected = true;
   writeConnectionTypePacket(1);
   // iSimulate monitor should respond with settings request and scenario request
}

void OnNewSimulationControl(AMM::SimulationControl& simControl, eprosima::fastrtps::SampleInfo_t* info) {
   std::string message;

   switch (simControl.type()) {
      case AMM::ControlType::RUN :

         // write last recorded SIM_TIME to monitor
         // TODO: iSimulate may need to fix. does not work as expected
         writeSyncTimesPacket();

         sim_status = 1;
         // requestedState 1 = running
         writeScenarioChangeStatePacket(sim_status);

         LOG_INFO << "SimControl Message recieved; Run sim.";
         break;

      case AMM::ControlType::HALT :

         sim_status = 2;
         // requestedState 2 = stopped
         writeScenarioChangeStatePacket(sim_status);

         LOG_INFO << "SimControl Message recieved; Halt sim.";
         break;

      case AMM::ControlType::RESET :

         //TODO: clear data and send to monitor before stopping
         nodeDataStorage.clear();
         // reset waveforms to default
         ecgWaveform = 9;
         bpWaveform = 0;
         spo2Waveform = 0;
         etco2Waveform = 0; 

         sim_status = 0;
         writeScenarioChangeStatePacket(0); // set monitor to pause state
         writeConnectionTypePacket(1);

         LOG_INFO << "SimControl Message recieved; Reset sim.";

         break;

      case AMM::ControlType::SAVE :
         // no action
         //LOG_INFO << "SimControl Message recieved; Save sim.";
         //mgr->WriteModuleConfiguration(currentState.mc);
         break;
   }
}

void OnNewTick(AMM::Tick& tick, eprosima::fastrtps::SampleInfo_t* info) {
   //if ( arguments.verbose )
   //   LOG_DEBUG << "Tick received!";
   if ( sim_status == 0 && tick.frame() > lastTick) {
      LOG_DEBUG << "Tick received! sim_status:" << sim_status << "->1 lastTick:" << lastTick << " tick.frame(): " << tick.frame();
      sim_status = 1;
      if ( websocket_connected && monitor_initialized ) writeScenarioChangeStatePacket(sim_status);
   }
   lastTick = tick.frame();
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
      if (physiologyvalue.name()=="SIM_TIME") {
         // reformat SIM_TIME for storage
         std::ostringstream oss;
         oss.precision(1);
         oss << std::fixed << physiologyvalue.value();
         nodeDataStorage["SIM_TIME"] = oss.str();
         //LOG_DEBUG << "sim time stringstream: " << oss.str();
         // send data if websocket connection to monitor is live
         if ( websocket_connected ) writeChangeActionPacket();
      }
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
   // testing mohses data connection
   static int printHFdata = 10;   // initialize counter to print first xx high freequency data points
   if ( arguments.verbose && printHFdata > 0) {
      LOG_DEBUG << "[AMM_Node_Data](HF) " << waveform.name() << "=" << waveform.value();
      printHFdata -= 1;
   }
}

void OnNewRenderModification(AMM::RenderModification &rendMod, SampleInfo_t *info) {
   // LOG_DEBUG << "Render Modification received:\n"
   //          << "Type:      " << rendMod.type() << "\n"
   //          << "Data:      " << rendMod.data();
   if ( rendMod.type()=="PATIENT_STATE_TACHYPNEA" ) {
      etco2Waveform = 2;
      LOG_INFO << "Patient entered state: Tachypnea. Setting EtCO2 waveform to 2 -> Obstructive 2";
      // waveform type updated on monitor with next ChangeActionPacket
   }
   if ( rendMod.type()=="PATIENT_STATE_TACHYCARDIA" ) {
      ecgWaveform = 14;
      LOG_INFO << "Patient entered state: Tachycardia. Setting ECG waveform to 14 -> Ventricular Tachycardia";
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

   // stop() will cause run() to return and leave the reconnect loop
   try_reconnect = false;
   ioc.stop();

   // Raise SIGTERM to trigger async signal handler
   //std::raise(SIGTERM);
}

int main(int argc, char *argv[]) {

   // set default command line options. process.
   arguments.monitor = 3;
   arguments.verbose = false;
   argp_parse(&argp, argc, argv, 0, 0, &arguments);

   static plog::ColorConsoleAppender<plog::TxtFormatter> consoleAppender;
   plog::init(plog::verbose, &consoleAppender);

   LOG_INFO << "=== [ iSimulate Bridge ] ===";
   LOG_INFO << "Monitor model ID = " << arguments.monitor;

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

   LOG_INFO << "iSimulate Bridge ready.";
   std::cout << "Listening for data... Press return to exit." << std::endl;

   while (try_reconnect) {

      // wait for updated service info
      while (try_reconnect) {
         if ( monitor_port !=0 && monitor_service_new) {
            LOG_INFO << "Monitor port aquired: " << monitor_port;
            LOG_INFO << "Monitor address aquired: " << monitor_address;
            if ( strlen(monitor_address) > 15 ) LOG_INFO << "Address too long (not IPv4): " << strlen(monitor_address);
            LOG_INFO << "Monitor address length: " << strlen(monitor_address);
            host = monitor_address;
            port = std::to_string(monitor_port);
            monitor_service_new = false;
            break;
         }
         std::this_thread::sleep_for(milliseconds(20));
      }

      LOG_INFO << "Connecting to iSimulate monitor.";

      // set up websocket session
      ws_session->run(host, port, target);
      ws_session->set_verbose( arguments.verbose );
      ws_session->registerHandshakeCallback(onWebsocketHandshake);
      ws_session->registerReadCallback(onNewWebsocketMessage);

      // Run the I/O context.
      // The call will return when the socket is closed.
      ioc.run();

      LOG_INFO << "Connection to iSimulate monitor closed.";
      websocket_connected = false;
      monitor_initialized = false;
      ioc.reset();
   }

   mgr->Shutdown();
   std::this_thread::sleep_for(milliseconds(100));
   delete mgr;

   LOG_INFO << "iSimulate Bridge shutdown.";
   return EXIT_SUCCESS;
}
