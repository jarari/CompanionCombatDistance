#include <Utilities.h>
#include <MathUtils.h>
#include <SimpleIni.h>
#include <fstream>
#include <unordered_map>
#include <wtypes.h>
using namespace RE;

CSimpleIniA ini(true, false, false);
PlayerCharacter* p;
PlayerCamera* pcam;
TESFaction* followerFaction = nullptr;
REL::Relocation<uintptr_t> ptr_CalculateDetectionRange{ REL::ID(1554285), 0xF };
REL::Relocation<uintptr_t> ptr_CheckShouldListen{ REL::ID(3975), 0x67 };
uintptr_t CalculateDetectionRangeOrig;
uintptr_t CheckShouldListenOrig;
float lastCachedTime = 0.f;
float cachedDetectionLevel = 0.f;
bool iniLoaded = false;

std::string CCD_ON = "Activated Aggressive Mode";
std::string CCD_OFF = "Deactivated Aggressive Mode";
const char* iniPath = "Data\\F4SE\\Plugins\\CCD.ini";

uint32_t modeSwitchKey = 0xa0;
int32_t combatSneakMinDetectionLevel = 75;
int32_t combatMinDetectionLevel = 15;
float combatDetectionRange = 15000.f;
bool aggressiveMode = true;

bool to_bool(std::string str)
{
	std::transform(str.begin(), str.end(), str.begin(), ::tolower);
	std::istringstream is(str);
	bool b;
	is >> std::boolalpha >> b;
	return b;
}

float ActorCalculateDetectionRange(Actor* a_actor, Actor* a_target, float a_offset) 
{
	if (a_actor->IsInFaction(followerFaction))
		return 50000.f;

	typedef float (*FnCalculateDetectionRange)(Actor*, Actor*, float);
	FnCalculateDetectionRange fn = (FnCalculateDetectionRange)CalculateDetectionRangeOrig;
	if (fn)
		return (*fn)(a_actor, a_target, a_offset);

	return 0.f;
}

bool CheckShouldListen(Actor* a_actor, Actor* a_target) 
{
	if (aggressiveMode &&
		p->IsInCombat() &&
		!a_target->IsDead(false) && 
		!a_actor->IsInCombatWithActor(a_target) &&
		a_actor->IsInFaction(followerFaction) &&
		Length(a_actor->data.location - a_target->data.location) <= combatDetectionRange &&
		a_target->GetShouldAttackActor(a_actor) &&
		CombatUtilities::CheckLOS(*a_actor, *a_target)) {

		if (*F4::ptr_engineTime - lastCachedTime >= 0.5f) {
			CombatManager* cm = CombatManager::GetSingleton();
			float lossPercentage = cm->GetTargetLostPercentage(p);
			if (lossPercentage >= 1.f) {
				cachedDetectionLevel = cm->GetStealthPoints(p) / 1.45f;
				//_MESSAGE("Caution %f", cachedDetectionLevel);
			} else {
				cachedDetectionLevel = lossPercentage * 100.f + 100.f;
				//_MESSAGE("Danger %f", cachedDetectionLevel);
			}
			lastCachedTime = *F4::ptr_engineTime;
		}

		float detectionLevel = max(min(100.f, cachedDetectionLevel), 0.f);
		bool isSneaking = p->IsSneaking();
		if ((isSneaking && detectionLevel >= combatSneakMinDetectionLevel) || (!isSneaking && detectionLevel >= combatMinDetectionLevel)) {
			a_actor->ForceDetect(a_target, false);
			a_actor->StartCombat(a_target);
			return true;
		}
	}

	typedef bool (*FnCheckShouldListen)(Actor*, Actor*);
	FnCheckShouldListen fn = (FnCheckShouldListen)CheckShouldListenOrig;
	if (fn)
		return (*fn)(a_actor, a_target);
	return false;
}

class InputEventReceiverOverride : public BSInputEventReceiver
{
public:
	typedef void (InputEventReceiverOverride::*FnPerformInputProcessing)(const InputEvent* a_queueHead);

	void ProcessButtonEvent(ButtonEvent* evn)
	{
		if (evn->eventType != INPUT_EVENT_TYPE::kButton) {
			if (evn->next)
				ProcessButtonEvent((ButtonEvent*)evn->next);
			return;
		}

		uint32_t id = evn->idCode;
		if (evn->device == INPUT_DEVICE::kMouse)
			id += 0x100;
		if (evn->device == INPUT_DEVICE::kGamepad)
			id += 0x10000;

		if (id == modeSwitchKey && evn->value > 0 && evn->heldDownSecs == 0.f) {
			aggressiveMode = !aggressiveMode;

			if (iniLoaded) {
				ini.SetBoolValue("Saved", "AggressiveMode", aggressiveMode);
				ini.SaveFile(iniPath, false);
			}

			if (aggressiveMode)
				SendHUDMessage::ShowHUDMessage(CCD_ON.c_str(), nullptr, true, true);
			else
				SendHUDMessage::ShowHUDMessage(CCD_OFF.c_str(), nullptr, true, true);
		}

		if (evn->next)
			ProcessButtonEvent((ButtonEvent*)evn->next);
	}

	void HookedPerformInputProcessing(const InputEvent* a_queueHead)
	{
		if (!UI::GetSingleton()->menuMode && a_queueHead) {
			ProcessButtonEvent((ButtonEvent*)a_queueHead);
		}
		FnPerformInputProcessing fn = fnHash.at(*(uint64_t*)this);
		if (fn) {
			(this->*fn)(a_queueHead);
		}
	}

	void HookSink()
	{
		uint64_t vtable = *(uint64_t*)this;
		auto it = fnHash.find(vtable);
		if (it == fnHash.end()) {
			FnPerformInputProcessing fn = SafeWrite64Function(vtable, &InputEventReceiverOverride::HookedPerformInputProcessing);
			fnHash.insert(std::pair<uint64_t, FnPerformInputProcessing>(vtable, fn));
		}
	}

protected:
	static std::unordered_map<uint64_t, FnPerformInputProcessing> fnHash;
};
std::unordered_map<uint64_t, InputEventReceiverOverride::FnPerformInputProcessing> InputEventReceiverOverride::fnHash;

void LoadConfigs()
{
	SI_Error result = ini.LoadFile(iniPath);
	if (result >= 0) {
		modeSwitchKey = std::stoi(ini.GetValue("General", "ModeSwitchKey", "0xBA"), 0, 16);
		combatMinDetectionLevel = std::stoi(ini.GetValue("General", "CombatMinDetectionLevel", "15"));
		combatSneakMinDetectionLevel = std::stoi(ini.GetValue("General", "CombatSneakMinDetectionLevel", "75"));
		combatDetectionRange = std::stof(ini.GetValue("General", "CombatDetectionRange", "15000.0"));
		aggressiveMode = to_bool(ini.GetValue("Saved", "AggressiveMode", "true"));
		CCD_ON = ini.GetValue("Translation", "CCD_ON", "Activated Aggressive Mode");
		CCD_OFF = ini.GetValue("Translation", "CCD_OFF", "Deactivated Aggressive Mode");
		iniLoaded = true;
	} else {
		_MESSAGE("Failed to load config.");
		iniLoaded = false;
	}
}

void InitializePlugin()
{
	p = PlayerCharacter::GetSingleton();
	pcam = PlayerCamera::GetSingleton();
	((InputEventReceiverOverride*)((uint64_t)pcam + 0x38))->HookSink();
	followerFaction = (TESFaction*)TESForm::GetFormByID(0x23C01);
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface * a_f4se, F4SE::PluginInfo * a_info) {
#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		return false;
	}

	*path /= fmt::format(FMT_STRING("{}.log"), Version::PROJECT);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

#ifndef NDEBUG
	log->set_level(spdlog::level::trace);
#else
	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::warn);
#endif

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("%g(%#): [%^%l%$] %v"s);

	logger::info(FMT_STRING("{} v{}"), Version::PROJECT, Version::NAME);

	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = Version::PROJECT.data();
	a_info->version = Version::MAJOR;

	if (a_f4se->IsEditor()) {
		logger::critical(FMT_STRING("loaded in editor"));
		return false;
	}

	const auto ver = a_f4se->RuntimeVersion();
	if (ver < F4SE::RUNTIME_1_10_162) {
		logger::critical(FMT_STRING("unsupported runtime v{}"), ver.string());
		return false;
	}

	F4SE::AllocTrampoline(64);

	return true;
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface * a_f4se) {
	F4SE::Init(a_f4se);

	F4SE::Trampoline& trampoline = F4SE::GetTrampoline();
	CalculateDetectionRangeOrig = trampoline.write_call<5>(ptr_CalculateDetectionRange.address(), &ActorCalculateDetectionRange);
	CheckShouldListenOrig = trampoline.write_call<5>(ptr_CheckShouldListen.address(), &CheckShouldListen);

	const F4SE::MessagingInterface* message = F4SE::GetMessagingInterface();
	message->RegisterListener([](F4SE::MessagingInterface::Message* msg) -> void {
		if (msg->type == F4SE::MessagingInterface::kGameDataReady) {
			InitializePlugin();
			LoadConfigs();
		} else if (msg->type == F4SE::MessagingInterface::kPostLoadGame) {
			LoadConfigs();
		} else if (msg->type == F4SE::MessagingInterface::kNewGame) {
			LoadConfigs();
		}
	});

	return true;
}
