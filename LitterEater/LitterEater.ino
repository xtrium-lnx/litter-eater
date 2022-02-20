#include <map>

#include <Preferences.h>

#include <WiFi.h>
#include <WiFiConnect.h>
#include <ESPAsyncWebServer.h>

#include <SPIFFS.h>
#include <AsyncElegantOTA.h>

#include <AsyncJson.h>
#include <ArduinoJson.h>

// #####################################################################################
// ## Pins

#define HALL_PIN_1      22
#define HALL_PIN_2      23
#define L298N_ENA       18
#define L298N_IN1       19
#define L298N_IN2       21
#define LOADSENSE_PIN   34

#define kLOADSENSOR_THRESHOLD 2500

// #####################################################################################
// ## Motor management

void SetupMotor()
{
    pinMode(L298N_ENA, OUTPUT);
    pinMode(L298N_IN1, OUTPUT);
    pinMode(L298N_IN2, OUTPUT);
}

void StartMotor(bool isBackwards)
{
    digitalWrite(L298N_IN2, isBackwards ? LOW : HIGH);
    digitalWrite(L298N_IN1, isBackwards ? HIGH : LOW);
    digitalWrite(L298N_ENA, HIGH);
}

void FreeMotor()
{
    digitalWrite(L298N_ENA, LOW);
}

void StopMotor()
{
    digitalWrite(L298N_IN1, LOW);
    digitalWrite(L298N_IN2, LOW);
    digitalWrite(L298N_ENA, HIGH);
    
    delay(100);
    digitalWrite(L298N_ENA, LOW);
}

// #####################################################################################
// ## Hall sensor management

void(*gHallCb1)();
void(*gHallCb2)();

int gHallPrev1 = LOW;
int gHallPrev2 = LOW;

void SetupHallSensors(void(*cb1)(), void(*cb2)())
{
    pinMode(HALL_PIN_1, INPUT);
    pinMode(HALL_PIN_2, INPUT);

    gHallCb1 = cb1;
    gHallCb2 = cb2;
}

void UpdateHallSensors()
{
    int pin1 = digitalRead(HALL_PIN_1);
    int pin2 = digitalRead(HALL_PIN_2);

    if (pin1 == LOW && gHallPrev1 == HIGH)
        gHallCb1();
        
    if (pin2 == LOW && gHallPrev2 == HIGH)
        gHallCb2();

    gHallPrev1 = pin1;
    gHallPrev2 = pin2;
}

// #####################################################################################
// State managemenent

void (*LoadSensor_OnLoadDetectionStart)();
void (*LoadSensor_OnLoadDetectionStop)();
int LoadSensor_PrevMeasure;

void SetupLoadSensor(void(*cbEnter)(), void(*cbLeave)())
{
    LoadSensor_OnLoadDetectionStart = cbEnter;
    LoadSensor_OnLoadDetectionStop  = cbLeave;
    LoadSensor_PrevMeasure    = 4095;
}

void LoadSensor_Update()
{
    int measure = analogRead(LOADSENSE_PIN);

    if (LoadSensor_PrevMeasure > kLOADSENSOR_THRESHOLD && measure < kLOADSENSOR_THRESHOLD)
        LoadSensor_OnLoadDetectionStart();

    if (LoadSensor_PrevMeasure < kLOADSENSOR_THRESHOLD && measure > kLOADSENSOR_THRESHOLD)
        LoadSensor_OnLoadDetectionStop();

    LoadSensor_PrevMeasure = measure;
}

// #####################################################################################
// State managemenent

int gCycleWaitDuration = 4; // minutes
int gCycleOvershoot    = 5; // seconds
int gEmptyOvershoot    = 5; // seconds


template<typename T>
class FSM
{
    T                                 m_previousState;
    T                                 m_currentState;
    T                                 m_nextState;

    std::map<T, void(*)(FSM*)>        m_states;
    std::map<uint16_t, void(*)(FSM*)> m_transitions;
    
public:
    FSM(T defaultState)
        : m_previousState(defaultState)
        , m_currentState(defaultState)
        , m_nextState(defaultState)
    {}

    void AddState(T state, void(*updateFn)(FSM*))
    {
        m_states[state] = updateFn;
    }
    
    void AddTransition(T from, T to, void(*transitionFn)(FSM*))
    {
        uint16_t transitionId = (((uint16_t)from) << 8) | (uint16_t)to;
        Serial.print("Adding transition: "); Serial.println(transitionId);
        if (m_transitions.count(transitionId) > 0)
            m_transitions[transitionId](this);
            
        m_transitions[transitionId] = transitionFn;
    }
    
    void Update()
    {
        if (m_nextState != m_currentState)
        {
            Serial.print("Transitioning: "); Serial.print((uint8_t)m_currentState); Serial.print(" => "); Serial.println((uint8_t)m_nextState);
            uint16_t transitionId = (((uint16_t)m_currentState) << 8) | (uint16_t)m_nextState;

            m_previousState = m_currentState;
            m_currentState  = m_nextState;
            
            if (m_transitions.count(transitionId) > 0)
            {
                m_transitions[transitionId](this);
            }
        }

        if (m_states.find(m_currentState) != m_states.end())
            m_states[m_currentState](this);
    }

    T PreviousState() { return m_previousState; }
    T GetState()      { return m_currentState;  }
    
    void Transition(T to)
    {
        m_nextState = to;
    }

    bool TransitionIf(T from, T to)
    {
        if (m_currentState == from)
        {
            m_nextState = to;
            return true;
        }

        return false;
    }
};

enum class ELitterMode
    : uint8_t
{
    Initializing,
    Idle,

    CatInside,
    WaitingForCycle,
    
    Cycling_Step1,
    Cycling_Step2,
    Cycling_Step2bis,
    Cycling_Step3,
    Cycling_CountdownToResume,
    Cycling_CST,
    
    Emptying_Step1,
    Emptying_Step2,
    Emptying_Step3,
    Emptying_CountdownToResume,
    Emptying_CST,

    Disabled
};

Preferences gLitterPrefs;

FSM<ELitterMode> gLitterFsm(ELitterMode::Initializing);
long             gWaitCatTimer;
unsigned long    gLastMillis;
ELitterMode      gInterruptedCycle;
ELitterMode      gInterruptedEmpty;
bool             gShouldDisable;

void Litter_OnHallSensorTriggered()
{
    Serial.println("Litter_OnHallSensorTriggered");
    gLitterFsm.TransitionIf(ELitterMode::Cycling_Step1, ELitterMode::Cycling_Step2);
    gLitterFsm.TransitionIf(ELitterMode::Cycling_Step2, ELitterMode::Cycling_Step2bis);
    gLitterFsm.TransitionIf(ELitterMode::Cycling_Step3, ELitterMode::Idle);
    
    gLitterFsm.TransitionIf(ELitterMode::Emptying_Step1, ELitterMode::Emptying_Step2);
    gLitterFsm.TransitionIf(ELitterMode::Emptying_Step3, ELitterMode::Idle);
}

void Litter_OnCatEntered()
{
    Serial.println("Litter_OnCatEntered");
    gLitterFsm.TransitionIf(ELitterMode::Idle,            ELitterMode::CatInside);
    gLitterFsm.TransitionIf(ELitterMode::WaitingForCycle, ELitterMode::CatInside);

    gLitterFsm.TransitionIf(ELitterMode::Cycling_Step1, ELitterMode::Cycling_CST);
    gLitterFsm.TransitionIf(ELitterMode::Cycling_Step2, ELitterMode::Cycling_CST);
    gLitterFsm.TransitionIf(ELitterMode::Cycling_CountdownToResume, ELitterMode::Cycling_CST);

    gLitterFsm.TransitionIf(ELitterMode::Emptying_Step1, ELitterMode::Emptying_CST);
    gLitterFsm.TransitionIf(ELitterMode::Emptying_Step3, ELitterMode::Emptying_CST);
    gLitterFsm.TransitionIf(ELitterMode::Emptying_CountdownToResume, ELitterMode::Emptying_CST);
}

void Litter_OnCatLeft()
{
    Serial.println("Litter_OnCatLeft");
    gLitterFsm.TransitionIf(ELitterMode::CatInside,    ELitterMode::WaitingForCycle);
    gLitterFsm.TransitionIf(ELitterMode::Cycling_CST,  ELitterMode::Cycling_CountdownToResume);
    gLitterFsm.TransitionIf(ELitterMode::Emptying_CST, ELitterMode::Emptying_CountdownToResume);
}

void Litter_Setup()
{
    SetupMotor();
    SetupHallSensors(Litter_OnHallSensorTriggered, Litter_OnHallSensorTriggered);
    SetupLoadSensor(Litter_OnCatEntered, Litter_OnCatLeft);

    gLitterFsm.AddState(ELitterMode::Initializing, [](FSM<ELitterMode>* fsm) {
        Serial.println("Initializing...");
        gWaitCatTimer = 0;
        gLastMillis   = 0;
        fsm->Transition(ELitterMode::WaitingForCycle);
        Serial.println("... done.");
    });
    
    gLitterFsm.AddState(ELitterMode::WaitingForCycle, [](FSM<ELitterMode>* fsm) {
        unsigned long now = millis();
        unsigned long dt  = now - gLastMillis;
        gLastMillis = now;
        
        gWaitCatTimer -= dt;
        if (gWaitCatTimer <= 0)
            fsm->Transition(ELitterMode::Cycling_Step1);
    });

    gLitterFsm.AddState(ELitterMode::Cycling_CountdownToResume, [](FSM<ELitterMode>* fsm) {
        unsigned long now = millis();
        unsigned long dt  = now - gLastMillis;
        gLastMillis = now;

        gWaitCatTimer -= dt;
        if (gWaitCatTimer <= 0)
            fsm->Transition(gInterruptedCycle);
    });

    gLitterFsm.AddState(ELitterMode::Emptying_CountdownToResume, [](FSM<ELitterMode>* fsm) {
        unsigned long now = millis();
        unsigned long dt  = now - gLastMillis;
        gLastMillis = now;

        gWaitCatTimer -= dt;
        if (gWaitCatTimer <= 0)
            fsm->Transition(gInterruptedEmpty);
    });


    gLitterFsm.AddTransition(ELitterMode::Idle,                       ELitterMode::Disabled,                  [](FSM<ELitterMode>* fsm) { gShouldDisable = false; });
    gLitterFsm.AddTransition(ELitterMode::WaitingForCycle,            ELitterMode::Disabled,                  [](FSM<ELitterMode>* fsm) { gShouldDisable = false; });
    gLitterFsm.AddTransition(ELitterMode::Disabled,                   ELitterMode::WaitingForCycle,           [](FSM<ELitterMode>* fsm) { gWaitCatTimer = 0; gLastMillis = 0; });

    gLitterFsm.AddTransition(ELitterMode::Idle,                       ELitterMode::CatInside,                 [](FSM<ELitterMode>* fsm) { gWaitCatTimer = gCycleWaitDuration * 1000 * 60; gLastMillis = millis(); });
    gLitterFsm.AddTransition(ELitterMode::WaitingForCycle,            ELitterMode::CatInside,                 [](FSM<ELitterMode>* fsm) { gWaitCatTimer = gCycleWaitDuration * 1000 * 60; gLastMillis = millis(); });
    
    gLitterFsm.AddTransition(ELitterMode::Idle,                       ELitterMode::Cycling_Step1,             [](FSM<ELitterMode>* fsm) { StartMotor(false); });
    gLitterFsm.AddTransition(ELitterMode::WaitingForCycle,            ELitterMode::Cycling_Step1,             [](FSM<ELitterMode>* fsm) { StartMotor(false); });
    gLitterFsm.AddTransition(ELitterMode::Cycling_CountdownToResume,  ELitterMode::Cycling_Step1,             [](FSM<ELitterMode>* fsm) { StartMotor(false); });
    gLitterFsm.AddTransition(ELitterMode::Cycling_Step1,              ELitterMode::Cycling_Step2,             [](FSM<ELitterMode>* fsm) { StopMotor(); delay(2000); StartMotor(true); });
    gLitterFsm.AddTransition(ELitterMode::Cycling_CountdownToResume,  ELitterMode::Cycling_Step2,             [](FSM<ELitterMode>* fsm) { StartMotor(true); });
    gLitterFsm.AddTransition(ELitterMode::Cycling_Step2,              ELitterMode::Cycling_Step2bis,          [](FSM<ELitterMode>* fsm) { delay(gCycleOvershoot * 1000); gLitterFsm.Transition(ELitterMode::Cycling_Step3); });
    gLitterFsm.AddTransition(ELitterMode::Cycling_Step2bis,           ELitterMode::Cycling_Step3,             [](FSM<ELitterMode>* fsm) { StopMotor(); delay(2000); StartMotor(false); });
    gLitterFsm.AddTransition(ELitterMode::Cycling_Step3,              ELitterMode::Idle,                      [](FSM<ELitterMode>* fsm) { StopMotor(); });
    gLitterFsm.AddTransition(ELitterMode::Cycling_Step1,              ELitterMode::Cycling_CST,               [](FSM<ELitterMode>* fsm) { gInterruptedCycle = fsm->PreviousState(); StopMotor(); });
    gLitterFsm.AddTransition(ELitterMode::Cycling_Step2,              ELitterMode::Cycling_CST,               [](FSM<ELitterMode>* fsm) { gInterruptedCycle = fsm->PreviousState(); StopMotor(); });
    gLitterFsm.AddTransition(ELitterMode::Cycling_CST,                ELitterMode::Cycling_CountdownToResume, [](FSM<ELitterMode>* fsm) { gWaitCatTimer = 10000; gLastMillis = millis(); });
    
    gLitterFsm.AddTransition(ELitterMode::Idle,                       ELitterMode::Emptying_Step1,             [](FSM<ELitterMode>* fsm) { StartMotor(true); });
    gLitterFsm.AddTransition(ELitterMode::WaitingForCycle,            ELitterMode::Emptying_Step1,             [](FSM<ELitterMode>* fsm) { StartMotor(true); });
    gLitterFsm.AddTransition(ELitterMode::Emptying_CountdownToResume, ELitterMode::Emptying_Step1,             [](FSM<ELitterMode>* fsm) { StartMotor(true); });
    gLitterFsm.AddTransition(ELitterMode::Emptying_Step1,             ELitterMode::Emptying_Step2,             [](FSM<ELitterMode>* fsm) { delay(gEmptyOvershoot * 1000); StopMotor(); });
    gLitterFsm.AddTransition(ELitterMode::Emptying_Step2,             ELitterMode::Emptying_Step3,             [](FSM<ELitterMode>* fsm) { StartMotor(false); });
    gLitterFsm.AddTransition(ELitterMode::Emptying_CountdownToResume, ELitterMode::Emptying_Step3,             [](FSM<ELitterMode>* fsm) { StartMotor(false); });
    gLitterFsm.AddTransition(ELitterMode::Emptying_Step3,             ELitterMode::Idle,                       [](FSM<ELitterMode>* fsm) { StopMotor(); });
    gLitterFsm.AddTransition(ELitterMode::Emptying_Step1,             ELitterMode::Emptying_CST,               [](FSM<ELitterMode>* fsm) { gInterruptedEmpty = fsm->PreviousState(); StopMotor(); });
    gLitterFsm.AddTransition(ELitterMode::Emptying_Step3,             ELitterMode::Emptying_CST,               [](FSM<ELitterMode>* fsm) { gInterruptedEmpty = fsm->PreviousState(); StopMotor(); });
    gLitterFsm.AddTransition(ELitterMode::Emptying_CST,               ELitterMode::Emptying_CountdownToResume, [](FSM<ELitterMode>* fsm) { gWaitCatTimer = 10000; gLastMillis = millis(); });

    gLitterPrefs.begin("litter-eater", false);
    gCycleWaitDuration = gLitterPrefs.getInt("waitDuration", 4);
    gCycleOvershoot    = gLitterPrefs.getInt("cycleOvershoot", 10);
    gEmptyOvershoot    = gLitterPrefs.getInt("emptyOvershoot", 7);
    gLitterPrefs.end();
    
    gWaitCatTimer = 0;

    gLitterFsm.Transition(ELitterMode::Initializing);
    Serial.println("Litter: Setup done.");
}

void Litter_Update()
{
    LoadSensor_Update();
    UpdateHallSensors();
    gLitterFsm.Update();

    if (gShouldDisable)
    {
        gLitterFsm.TransitionIf(ELitterMode::Idle, ELitterMode::Disabled);
        gLitterFsm.TransitionIf(ELitterMode::WaitingForCycle, ELitterMode::Disabled);
    }
}

// #####################################################################################
// ## Network configuration

AsyncWebServer server(80);
WiFiConnect wc;

bool Wifi_Setup()
{
    Serial.println("Configuring Wifi...");
    delay(5000);
    wc.setDebug(true);
    wc.setAPCallback([](WiFiConnect*) { Serial.println("Entering AP mode"); });
    //wc.resetSettings();

    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.setHostname("litter-eater");
    
    if (!wc.autoConnect())
        wc.startConfigurationPortal(AP_WAIT);

    return true;
}

void HttpServer_Setup()
{
    if (!SPIFFS.begin(true))
    {
        Serial.println("Unable to mount SPIFFS");
        return;
    }
    
    server.serveStatic("/", SPIFFS, "/www/").setDefaultFile("index.html");

    server.on("/stats", HTTP_GET, [](AsyncWebServerRequest* req) {
        Serial.println("Http: /stats");
        StaticJsonDocument<96> json;
        json["state"]          = int(gLitterFsm.GetState());
        json["currentLoad"]    = LoadSensor_PrevMeasure;
        json["waitDuration"]   = gCycleWaitDuration;
        json["cycleOvershoot"] = gCycleOvershoot;
        json["emptyOvershoot"] = gEmptyOvershoot;

        String result;
        serializeJson(json, result);
        req->send(200, "application/json", result);
    });
    
    server.on("/cycle", HTTP_GET, [](AsyncWebServerRequest* req) {
        Serial.println("Http: /cycle");
        bool hasTransitioned = gLitterFsm.TransitionIf(ELitterMode::Idle, ELitterMode::Cycling_Step1);
    
        if (!hasTransitioned)
            hasTransitioned = gLitterFsm.TransitionIf(ELitterMode::WaitingForCycle, ELitterMode::Cycling_Step1);

        req->redirect("/");
    });

    server.on("/save_settings", HTTP_GET, [](AsyncWebServerRequest* req) {
        gLitterPrefs.begin("litter-eater", false);
    
        if (req->hasParam("waitDuration"))
        {
            gCycleWaitDuration = req->getParam("waitDuration")->value().toInt();
            gLitterPrefs.putInt("waitDuration", gCycleWaitDuration);
        }

        if (req->hasParam("cycleOvershoot"))
        {
            gCycleOvershoot = req->getParam("cycleOvershoot")->value().toInt();
            gLitterPrefs.putInt("cycleOvershoot", gCycleOvershoot);
        }

        if (req->hasParam("emptyOvershoot"))
        {
            gEmptyOvershoot = req->getParam("emptyOvershoot")->value().toInt();
            gLitterPrefs.putInt("emptyOvershoot", gEmptyOvershoot);
        }
            
        gLitterPrefs.end();
        req->send(200, "application/json", "{\"result\": true}");
    });
    
    server.on("/empty", HTTP_GET, [](AsyncWebServerRequest* req) {
        Serial.println("Http: /empty");
        bool hasTransitioned = gLitterFsm.TransitionIf(ELitterMode::Idle, ELitterMode::Emptying_Step1);
    
        if (!hasTransitioned)
            hasTransitioned = gLitterFsm.TransitionIf(ELitterMode::WaitingForCycle, ELitterMode::Emptying_Step1);
    
        req->redirect("/");
    });
    
    server.on("/reset", HTTP_GET, [](AsyncWebServerRequest* req) {
        Serial.println("Http: /reset");
        gLitterFsm.TransitionIf(ELitterMode::Emptying_Step2, ELitterMode::Emptying_Step3);
        req->redirect("/");
    });

    server.on("/isenabled", HTTP_GET, [](AsyncWebServerRequest* req) {
        Serial.println("Http: /isenabled");
        StaticJsonDocument<16> json;
        json["result"] = gLitterFsm.GetState() != ELitterMode::Disabled;

        String result;
        serializeJson(json, result);
        req->send(200, "application/json", result);
    });

    server.on("/enable", HTTP_GET, [](AsyncWebServerRequest* req) {
        Serial.println("Http: /enable");
        gLitterFsm.TransitionIf(ELitterMode::Disabled, ELitterMode::WaitingForCycle);
        req->send(200, "application/json", "{\"result\": true}");
    });

    server.on("/disable", HTTP_GET, [](AsyncWebServerRequest* req) {
        Serial.println("Http: /disable");
        if (gLitterFsm.GetState() != ELitterMode::Disabled)
            gShouldDisable = true;
            
        req->send(200, "application/json", "{\"result\": true}");
    });

    AsyncElegantOTA.begin(&server);

    server.begin();
}

// #####################################################################################
// ## Arduino entrypoints

void setup()
{
    Serial.begin(115200);
    Litter_Setup();

    if (Wifi_Setup())
        HttpServer_Setup();
}

void loop()
{
    Litter_Update();
    delay(50);
}
