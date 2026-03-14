# Support WH-1000XM4 — Protocole V1 : Documentation & Plan d'implémentation

## 1. Contexte

Le rewrite a changé le service UUID Bluetooth pour ne cibler que les appareils XM5+ (`956C7B26-D49A-4BA8-B03F-B17D393CB6E2`). Le WH-1000XM4 (et XM3, WF-1000XM4, etc.) utilise l'ancien UUID RFCOMM (`96CC203E-5068-46AD-B32D-E316F5E069BA`). C'est le **seul** changement ayant brisé la compatibilité au niveau transport — le reste est du travail de protocole.

---

## 2. Analyse comparative : ce qui est identique, ce qui diffère

### Ce qui est **identique** entre V1 et V2

| Élément | Valeur | Source |
|---------|--------|--------|
| Transport Linux | RFCOMM + SDP (déjà dans `PlatformLinux.cpp`) | Code existant |
| Framing paquet | `START=0x3E`, `END=0x3C`, escape `0x3D` | Plutoberth Constants.h = Protocol.hpp actuel |
| Escape sequences | `kEscaped60=0x2C`, `kEscaped61=0x2D`, `kEscaped62=0x2E` | Identiques |
| Checksum | Sum of all payload bytes | Identique |
| Type de paquet | `DATA_MDR = 0x0C` pour les commandes | Identique |
| Bytes de commande NC | `NCASM_SET_PARAM = 0x68` | Identique dans V2T1 |
| Structure de l'enum `MDRDataType` | Identique (DATA, ACK, DATA_MDR, etc.) | Plutoberth = Protocol.hpp |

### Ce qui **diffère**

| Élément | V1 | V2 |
|---------|----|----|
| Service UUID | `96CC203E-5068-46AD-B32D-E316F5E069BA` | `956C7B26-D49A-4BA8-B03F-B17D393CB6E2` |
| Protocol Tables | Aucune (un seul espace de commandes) | Table 1 + Table 2 (`DATA_MDR_NO2`) |
| Init sequence | Pas de négociation de support functions | `CONNECT_GET_SUPPORT_FUNCTION` obligatoire |
| NC/ASM payload | 8 bytes, structure simple | Structs typés avec variants (NA, Seamless, etc.) |
| VPT (surround) | `VPT_SET_PARAM = 0x48`, preset | Absent en V2 |
| Features avancées | Absentes (pas de STC, touch, head gesture, EQ avancé) | Présentes selon support |
| Réponse protocol info | Simpler / version basse | Version + table flags |

---

## 3. Spécifications protocole V1

### 3.1 Service UUID et connexion

```
UUID : 96CC203E-5068-46AD-B32D-E316F5E069BA
Bytes : 0x96,0xCC,0x20,0x3E,0x50,0x68,0x46,0xAD,0xB3,0x2D,0xE3,0x16,0xF5,0xE0,0x69,0xBA
Transport : RFCOMM sur Bluetooth Classic (SDP lookup → canal RFCOMM dynamique)
```

Le `PlatformLinux.cpp` existant fait exactement cette opération — il prend n'importe quel UUID, fait une requête SDP, et ouvre un socket RFCOMM. **Aucun changement sur Linux.**

### 3.2 Framing des paquets (identique à V2)

```
[0x3E] [ESCAPED(TYPE:1 | SEQ:1 | SIZE_BE:4 | DATA:N | CHECKSUM:1)] [0x3C]
```

`MDRPackCommand` / `MDRUnpackCommand` dans `Command.cpp` sont **réutilisables sans modification**.

### 3.3 Commandes V1 connues

#### Initialisation (séquence au démarrage)

V1 n'a pas de table de support functions négociée. La séquence de connexion recommandée :

```
→ CONNECT_GET_PROTOCOL_INFO (0x00)   via DATA_MDR
← CONNECT_RET_PROTOCOL_INFO (0x01)  [version byte faible, pas de table flags]
→ POWER_GET_STATUS (0x22)            type=BATTERY
← POWER_RET_STATUS (0x23)            [level + charging]
→ NCASM_GET_PARAM (0x66)             type=NC_ASM (0x02)
← NCASM_RET_PARAM (0x67)             [état courant NC/ASM]
```

#### Commande NC/ASM — `NCASM_SET_PARAM (0x68)` — payload 8 bytes

```
[0] NCASM_SET_PARAM = 0x68
[1] NC_ASM_INQUIRED_TYPE :
      0x00 = NO_USE
      0x01 = NOISE_CANCELLING seul
      0x02 = NC + ASM (mode combiné)
      0x03 = AMBIENT_SOUND seul
[2] NC_ASM_EFFECT :
      0x00 = OFF
      0x01 = ON
      0x10 = ADJUSTMENT_IN_PROGRESS
      0x11 = ADJUSTMENT_COMPLETION
[3] NC_ASM_SETTING_TYPE :
      0x00 = ON_OFF
      0x01 = LEVEL_ADJUSTMENT
[4] NC_DUAL_SINGLE :
      0x00 = DUAL  (asmLevel == 0)
      0x01 = SINGLE (asmLevel >= 1)
[5] ASM_SETTING_TYPE :
      0x00 = ON_OFF
      0x01 = LEVEL_ADJUSTMENT
[6] ASM_ID :
      0x00 = VOICE
      0x01 = NORMAL
[7] ASM_LEVEL : uint8  [0..19] (MAX_STEPS_WH_1000_XM3 = 19)
```

#### Commande VPT — `VPT_SET_PARAM (0x48)` — payload 3 bytes

```
[0] VPT_SET_PARAM = 0x48
[1] VPT_INQUIRED_TYPE :
      0x01 = VPT (surround)
      0x02 = SOUND_POSITION
[2] PRESET_ID :
      0x00 = OFF
      0x01 = OUTDOOR_FESTIVAL
      0x02 = ARENA
      0x03 = CONCERT_HALL
      0x04 = CLUB
```
*(Positions spatiales : OFF=0x00, FRONT_LEFT=0x01, FRONT_RIGHT=0x02, FRONT=0x03, REAR_LEFT=0x11, REAR_RIGHT=0x12)*

#### Batterie — `POWER_GET_STATUS (0x22)` / `POWER_RET_STATUS (0x23)`

Structure réponse (à préciser par capture Wireshark) :
```
[0] POWER_RET_STATUS = 0x23
[1] INQUIRED_TYPE = 0x00 (BATTERY)
[2] battery_level  : uint8 [0..100]
[3] charging_status : 0x00=not charging, 0x01=charging
```

---

## 4. Découverte clé : le transport est déjà prêt

```
libmdr/src/Platform/Linux/PlatformLinux.cpp:45
  ptr->fd = socket(AF_BLUETOOTH, SOCK_STREAM | SOCK_NONBLOCK, BTPROTO_RFCOMM);
  ...
  ptr->sdpSession = sdp_connect_nb(macAddress);  // SDP lookup avec l'UUID fourni
```

Il suffit de passer `MDR_SERVICE_UUID_XM4` à `mdrConnectionConnect()` — le reste fonctionne.

**Windows** et **macOS** utilisent également RFCOMM Classic BT dans leurs implémentations respectives (même pattern que le prédécesseur Plutoberth). **Web/Emscripten** ne supporte pas RFCOMM → **V1 sera natif uniquement.**

---

## 5. Détection de version au moment de la connexion

**Approche retenue : UUID-based (simple et fiable)**

| UUID utilisé | Protocole activé |
|-------------|-----------------|
| `MDR_SERVICE_UUID_XM5` | V2 (comportement actuel) |
| `MDR_SERVICE_UUID_XM4` | V1 (nouveau) |

L'`MDRHeadphones` reçoit un flag `protocolHint` à la création, ou le client choisit via l'UI. Pas de fallback automatique — la sélection du device dans l'UI détermine l'UUID.

Alternative future : envoyer `CONNECT_GET_PROTOCOL_INFO` et examiner le `protocolVersion` en réponse pour détecter automatiquement — mais c'est complexe et non prioritaire.

---

## 6. Plan d'implémentation détaillé

### Phase 0 — Vérification terrain (avant tout code)

Avant d'implémenter, capturer un échange réel avec le WH-1000XM4 sous Linux :

```bash
# Activer HCI sniffing
sudo btmon -w /tmp/xm4_capture.btsnoop &

# Connecter avec bluetoothctl ou l'app Sony officielle sous Linux
# Ensuite analyser avec Wireshark (filtre: btl2cap ou rfcomm)
```

Objectif : confirmer les payload structures des commandes NC/ASM et battery responses, et identifier s'il y a une init sequence.

---

### Phase 1 — Constante UUID dans `Base.h`

**Fichier** : `libmdr/include/mdr-c/Base.h`

```diff
 // Service UUIDs
 // XM5s and newer
 #define MDR_SERVICE_UUID_XM5 "956C7B26-D49A-4BA8-B03F-B17D393CB6E2"
+// Legacy V1 devices: WH-1000XM4, WH-1000XM3, WF-1000XM4, etc.
+#define MDR_SERVICE_UUID_XM4 "96CC203E-5068-46AD-B32D-E316F5E069BA"
```

---

### Phase 2 — Header de protocole V1

**Nouveau fichier** : `libmdr/include/mdr/ProtocolV1.hpp`

Modèle exact de `ProtocolV2.hpp`. Contenu minimal pour WH-1000XM4 :

```cpp
#pragma once
#include "Protocol.hpp"
#pragma pack(push, 1)

namespace mdr::v1
{
    enum class Command : UInt8
    {
        CONNECT_GET_PROTOCOL_INFO = 0x00,
        CONNECT_RET_PROTOCOL_INFO = 0x01,
        POWER_GET_STATUS          = 0x22,
        POWER_RET_STATUS          = 0x23,
        POWER_NTFY_STATUS         = 0x25,
        NCASM_GET_PARAM           = 0x66,
        NCASM_RET_PARAM           = 0x67,
        NCASM_SET_PARAM           = 0x68,
        NCASM_NTFY_PARAM          = 0x69,
        VPT_GET_PARAM             = 0x46,   // à confirmer
        VPT_RET_PARAM             = 0x47,   // à confirmer
        VPT_SET_PARAM             = 0x48,
        VPT_NTFY_PARAM            = 0x49,   // à confirmer
    };

    enum class NcAsmInquiredType : UInt8
    {
        NO_USE                             = 0x00,
        NOISE_CANCELLING                   = 0x01,
        NOISE_CANCELLING_AND_AMBIENT_SOUND = 0x02,
        AMBIENT_SOUND                      = 0x03,
    };

    enum class NcAsmEffect : UInt8
    {
        OFF                    = 0x00,
        ON                     = 0x01,
        ADJUSTMENT_IN_PROGRESS = 0x10,
        ADJUSTMENT_COMPLETION  = 0x11,
    };

    enum class NcAsmSettingType : UInt8
    {
        ON_OFF           = 0x00,
        LEVEL_ADJUSTMENT = 0x01,
    };

    enum class NcDualSingle : UInt8
    {
        DUAL   = 0x00,
        SINGLE = 0x01,
    };

    enum class AsmId : UInt8
    {
        VOICE  = 0x00,
        NORMAL = 0x01,
    };

    enum class VptInquiredType : UInt8
    {
        VPT            = 0x01,
        SOUND_POSITION = 0x02,
    };

    enum class VptPresetId : UInt8
    {
        OFF              = 0x00,
        OUTDOOR_FESTIVAL = 0x01,
        ARENA            = 0x02,
        CONCERT_HALL     = 0x03,
        CLUB             = 0x04,
    };

    enum class SoundPositionPreset : UInt8
    {
        OFF         = 0x00,
        FRONT_LEFT  = 0x01,
        FRONT_RIGHT = 0x02,
        FRONT       = 0x03,
        REAR_LEFT   = 0x11,
        REAR_RIGHT  = 0x12,
    };

    // --- Payloads ---

    struct ConnectGetProtocolInfo
    {
        Command command = Command::CONNECT_GET_PROTOCOL_INFO;
        MDR_DEFINE_TRIVIAL_SERIALIZATION(ConnectGetProtocolInfo);
    };

    struct ConnectRetProtocolInfo
    {
        Command command;
        UInt8   protocolVersion;   // e.g. 0x01 for V1
        MDR_DEFINE_TRIVIAL_SERIALIZATION(ConnectRetProtocolInfo);
    };

    struct PowerGetStatus
    {
        Command command = Command::POWER_GET_STATUS;
        UInt8   inquiredType = 0x00; // 0x00 = BATTERY
        MDR_DEFINE_TRIVIAL_SERIALIZATION(PowerGetStatus);
    };

    struct PowerRetStatusBattery
    {
        Command command;
        UInt8   inquiredType;
        UInt8   batteryLevel;    // 0..100
        UInt8   chargingStatus;  // 0=not charging, 1=charging
        MDR_DEFINE_TRIVIAL_SERIALIZATION(PowerRetStatusBattery);
    };

    struct NcAsmGetParam
    {
        Command          command      = Command::NCASM_GET_PARAM;
        NcAsmInquiredType inquiredType = NcAsmInquiredType::NOISE_CANCELLING_AND_AMBIENT_SOUND;
        MDR_DEFINE_TRIVIAL_SERIALIZATION(NcAsmGetParam);
    };

    struct NcAsmSetParam
    {
        Command          command;
        NcAsmInquiredType inquiredType;
        NcAsmEffect       ncAsmEffect;
        NcAsmSettingType  ncAsmSettingType;
        NcDualSingle      ncDualSingle;
        NcAsmSettingType  asmSettingType;
        AsmId             asmId;
        UInt8             asmLevel;      // 0..19
        MDR_DEFINE_TRIVIAL_SERIALIZATION(NcAsmSetParam);
    };
    using NcAsmRetParam  = NcAsmSetParam; // Same layout
    using NcAsmNtfyParam = NcAsmSetParam;

    struct VptSetParam
    {
        Command        command;
        VptInquiredType inquiredType;
        UInt8           preset;  // VptPresetId or SoundPositionPreset
        MDR_DEFINE_TRIVIAL_SERIALIZATION(VptSetParam);
    };
    using VptGetParam  = VptSetParam;
    using VptRetParam  = VptSetParam;
    using VptNtfyParam = VptSetParam;

} // namespace mdr::v1

#pragma pack(pop)
```

> **Note** : Tous les structs V1 utilisent `MDR_DEFINE_TRIVIAL_SERIALIZATION` — pas de codegen LLVM nécessaire pour V1 car tous les payloads sont des POD à taille fixe.

---

### Phase 3 — Nouvelles propriétés dans `Headphones.hpp`

**Fichier** : `libmdr/include/mdr/Headphones.hpp`

Ajouter les membres V1-spécifiques dans `MDRHeadphones` :

```cpp
// --- V1 Protocol State ---
bool mIsV1Protocol = false;  // Set at connect time based on UUID

// V1-only battery (simpler than V2's multi-slot battery)
struct V1BatteryState { uint8_t level = 0; bool charging = false; };
V1BatteryState mV1Battery;

// V1 VPT
MDRProperty<v1::VptPresetId>        mVptPreset;
MDRProperty<v1::SoundPositionPreset> mSoundPosition;
```

Ajouter les déclarations de méthodes :

```cpp
MDRTask RequestInitV1();
MDRTask RequestSyncV1();
MDRTask RequestCommitV1();

int HandleCommandV1(Span<const UInt8> cmd, MDRCommandSeqNumber seq);
```

Ajouter l'awaiter V1 (réutiliser `AWAIT_PROTOCOL_INFO` existant ou ajouter) :

```cpp
// Dans enum AwaitType :
AWAIT_V1_BATTERY,
AWAIT_V1_NCASM,
```

---

### Phase 4 — Routing dans `Handle()`

**Fichier** : `libmdr/src/Headphones.cpp`, méthode `Handle()` (ligne ~106)

```cpp
int MDRHeadphones::Handle(Span<const UInt8> command, MDRDataType type, MDRCommandSeqNumber seq)
{
    using enum MDRDataType;
    mSeqNumber = seq;
    switch (type)
    {
    case ACK:
        HandleAck(seq);
        break;
    case DATA_MDR:
        SendACK(seq);
        // V1 et V2T1 utilisent tous les deux DATA_MDR.
        // On distingue via le flag positionné à la connexion.
        if (mIsV1Protocol)
            return HandleCommandV1(command, seq);
        return HandleCommandV2T1(command, seq);
    case DATA_MDR_NO2:
        SendACK(seq);
        return HandleCommandV2T2(command, seq);
    default:
        break;
    }
    return MDR_HEADPHONES_EVT_UNHANDLED;
}
```

---

### Phase 5 — Handler V1

**Nouveau fichier** : `libmdr/src/HeadphonesV1.cpp`

```cpp
#include <mdr/Headphones.hpp>

namespace mdr
{
    using namespace v1;

    // --- Command dispatch ---

    int MDRHeadphones::HandleCommandV1(Span<const UInt8> cmd, MDRCommandSeqNumber seq)
    {
        if (cmd.empty()) return MDR_HEADPHONES_EVT_UNHANDLED;
        switch (static_cast<Command>(cmd[0]))
        {
        case Command::CONNECT_RET_PROTOCOL_INFO:
            Awake(AWAIT_PROTOCOL_INFO);
            return MDR_HEADPHONES_EVT_OK;

        case Command::POWER_RET_STATUS:
        case Command::POWER_NTFY_STATUS:
        {
            PowerRetStatusBattery res;
            PowerRetStatusBattery::Deserialize(cmd.data(), res, cmd.size());
            mV1Battery.level    = res.batteryLevel;
            mV1Battery.charging = res.chargingStatus != 0;
            Awake(AWAIT_V1_BATTERY);
            return MDR_HEADPHONES_EVT_BATTERY;
        }

        case Command::NCASM_RET_PARAM:
        case Command::NCASM_NTFY_PARAM:
        {
            NcAsmRetParam res;
            NcAsmRetParam::Deserialize(cmd.data(), res, cmd.size());
            mNcAsmEnabled.overwrite(res.ncAsmEffect == NcAsmEffect::ON);
            mNcAsmMode.overwrite(/* map NcAsmInquiredType → internal enum */);
            mNcAsmFocusOnVoice.overwrite(res.asmId == AsmId::VOICE);
            mNcAsmAmbientLevel.overwrite(static_cast<int>(res.asmLevel));
            mNcAsmEnabled.commit(); mNcAsmMode.commit();
            mNcAsmFocusOnVoice.commit(); mNcAsmAmbientLevel.commit();
            Awake(AWAIT_V1_NCASM);
            return MDR_HEADPHONES_EVT_NCASM_PARAM;
        }

        case Command::VPT_RET_PARAM:
        case Command::VPT_NTFY_PARAM:
        {
            VptRetParam res;
            VptRetParam::Deserialize(cmd.data(), res, cmd.size());
            if (res.inquiredType == VptInquiredType::VPT)
                mVptPreset.overwrite(static_cast<VptPresetId>(res.preset));
            else
                mSoundPosition.overwrite(static_cast<SoundPositionPreset>(res.preset));
            mVptPreset.commit(); mSoundPosition.commit();
            return MDR_HEADPHONES_EVT_OK;
        }

        default:
            return MDR_HEADPHONES_EVT_UNHANDLED;
        }
    }

    // --- Init / Sync / Commit coroutines ---

    MDRTask MDRHeadphones::RequestInitV1()
    {
        // 1. Protocol handshake
        ConnectGetProtocolInfo req;
        SendCommandACK(ConnectGetProtocolInfo, req);
        co_await Await(AWAIT_PROTOCOL_INFO);

        // 2. Query NC/ASM state
        SendCommandACK(NcAsmGetParam, NcAsmGetParam{});
        co_await Await(AWAIT_V1_NCASM);

        // 3. Query VPT state
        VptGetParam vptReq;
        vptReq.command      = Command::VPT_GET_PARAM;
        vptReq.inquiredType = VptInquiredType::VPT;
        SendCommandACK(VptGetParam, vptReq);

        co_return MDR_HEADPHONES_TASK_INIT_OK;
    }

    MDRTask MDRHeadphones::RequestSyncV1()
    {
        // Battery
        SendCommandACK(PowerGetStatus, PowerGetStatus{});
        co_await Await(AWAIT_V1_BATTERY);

        co_return MDR_HEADPHONES_TASK_SYNC_OK;
    }

    MDRTask MDRHeadphones::RequestCommitV1()
    {
        // NC/ASM
        if (mNcAsmEnabled.dirty() || mNcAsmAmbientLevel.dirty() ||
            mNcAsmFocusOnVoice.dirty() || mNcAsmMode.dirty())
        {
            NcAsmSetParam res;
            res.command = Command::NCASM_SET_PARAM;

            bool ncOn  = (/* mode includes NC */);
            bool asmOn = (/* mode includes ASM */);

            if (ncOn && asmOn)
                res.inquiredType = NcAsmInquiredType::NOISE_CANCELLING_AND_AMBIENT_SOUND;
            else if (ncOn)
                res.inquiredType = NcAsmInquiredType::NOISE_CANCELLING;
            else if (asmOn)
                res.inquiredType = NcAsmInquiredType::AMBIENT_SOUND;
            else
                res.inquiredType = NcAsmInquiredType::NO_USE;

            res.ncAsmEffect      = mNcAsmEnabled.desired ? NcAsmEffect::ON : NcAsmEffect::OFF;
            res.ncAsmSettingType = NcAsmSettingType::LEVEL_ADJUSTMENT;
            res.asmLevel         = static_cast<UInt8>(mNcAsmAmbientLevel.desired);
            res.ncDualSingle     = res.asmLevel == 0 ? NcDualSingle::DUAL : NcDualSingle::SINGLE;
            res.asmSettingType   = NcAsmSettingType::LEVEL_ADJUSTMENT;
            res.asmId            = mNcAsmFocusOnVoice.desired ? AsmId::VOICE : AsmId::NORMAL;

            SendCommandACK(NcAsmSetParam, res);
            mNcAsmEnabled.commit(); mNcAsmAmbientLevel.commit();
            mNcAsmFocusOnVoice.commit(); mNcAsmMode.commit();
        }

        // VPT
        if (mVptPreset.dirty())
        {
            VptSetParam res;
            res.command      = Command::VPT_SET_PARAM;
            res.inquiredType = VptInquiredType::VPT;
            res.preset       = static_cast<UInt8>(mVptPreset.desired);
            SendCommandACK(VptSetParam, res);
            mVptPreset.commit();
        }

        co_return MDR_HEADPHONES_TASK_COMMIT_OK;
    }
}
```

---

### Phase 6 — C API dans `Headphones.cpp`

**Fichier** : `libmdr/src/Headphones.cpp`, section `C Exports`

```cpp
int mdrHeadphonesSetProtocolV1(MDRHeadphones* p, int isV1)
{
    auto h = reinterpret_cast<mdr::MDRHeadphones*>(p);
    h->mIsV1Protocol = (isV1 != 0);
    return MDR_RESULT_OK;
}

int mdrHeadphonesRequestInitV1(MDRHeadphones* p)
{
    auto h = reinterpret_cast<mdr::MDRHeadphones*>(p);
    return h->Invoke(h->RequestInitV1());
}

int mdrHeadphonesRequestSyncV1(MDRHeadphones* p)
{
    auto h = reinterpret_cast<mdr::MDRHeadphones*>(p);
    return h->Invoke(h->RequestSyncV1());
}

int mdrHeadphonesRequestCommitV1(MDRHeadphones* p)
{
    auto h = reinterpret_cast<mdr::MDRHeadphones*>(p);
    return h->Invoke(h->RequestCommitV1());
}
```

Déclarer ces fonctions dans `libmdr/include/mdr-c/Headphones.h`.

---

### Phase 7 — Build system : pas de codegen V1

Tous les structs V1 utilisent `MDR_DEFINE_TRIVIAL_SERIALIZATION` (POD fixe). Pas besoin d'ajouter des règles codegen LLVM.

**Fichier** : `libmdr/src/CMakeLists.txt`

Uniquement ajouter `HeadphonesV1.cpp` à la liste des sources :

```cmake
# Dans la liste des SOURCES existante (via GLOB ou explicitement) :
# HeadphonesV1.cpp sera inclus automatiquement si GLOB *.cpp est utilisé.
# Sinon l'ajouter explicitement.
```

---

### Phase 8 — UI Client

**Fichier** : à identifier dans `client/`

1. Dans la logique de sélection de device, distinguer les devices V1 vs V2 :
   - Heuristique sur le nom : "WH-1000XM4", "WH-1000XM3", "WF-1000XM4"
   - Ou : proposer deux UUIDs dans un menu de sélection manuelle

2. Avant `mdrHeadphonesRequestInitV2()`, appeler `mdrHeadphonesSetProtocolV1(h, 1)` si l'UUID XM4 est utilisé, puis `mdrHeadphonesRequestInitV1()`.

3. Afficher dans l'UI les propriétés V1-specific (VPT, batterie simple) si `mIsV1Protocol`.

---

## 7. Ordre d'exécution recommandé

```
[1] Capture Wireshark XM4  ──────►  Valider les payloads NC et batterie
         │
         ▼
[2] Base.h : ajouter UUID
         │
         ▼
[3] ProtocolV1.hpp : structs (ajuster après capture Wireshark)
         │
         ▼
[4] Headphones.hpp : nouveaux membres + déclarations
         │
         ▼
[5] Headphones.cpp : routing Handle() + C API
         │
         ▼
[6] HeadphonesV1.cpp : handlers + coroutines
         │
         ▼
[7] Build + test connexion XM4 (init V1 seul)
         │
         ▼
[8] Commit/Sync : implémenter et tester NC toggle
         │
         ▼
[9] Client UI : sélection protocole + display
```

---

## 8. Risques et inconnues

| Risque | Impact | Mitigation |
|--------|--------|------------|
| Structure exacte des payloads battery/VPT inconnue | Bloquant pour phase 6 | Wireshark capture en étape 1 |
| Init sequence V1 peut différer (pas de `CONNECT_GET_PROTOCOL_INFO`) | Coroutine bloquée | Timeout sur l'awaiter + fallback |
| Windows/macOS : RFCOMM via leur UUID V1 à tester | Non-Linux peut ne pas fonctionner | Tester sur Linux d'abord |
| `mNcAsmMode` : type interne V2 peut ne pas mapper proprement vers V1 | Mauvaise valeur envoyée | Définir un enum V1 dédié ou mapper explicitement |
| Emscripten | Web Serial ne supporte pas RFCOMM Classic BT | Ne pas exposer V1 dans la build Web |

---

## 9. Fichiers à créer / modifier — récapitulatif

| Fichier | Action | Effort estimé |
|---------|--------|---------------|
| `libmdr/include/mdr-c/Base.h` | +1 ligne UUID | Trivial |
| `libmdr/include/mdr/ProtocolV1.hpp` | Nouveau | ~150 lignes |
| `libmdr/include/mdr/Headphones.hpp` | +membres + déclarations | ~30 lignes |
| `libmdr/include/mdr-c/Headphones.h` | +4 déclarations C | ~10 lignes |
| `libmdr/src/Headphones.cpp` | Routing Handle() + C exports | ~25 lignes |
| `libmdr/src/HeadphonesV1.cpp` | Nouveau — handlers + 3 coroutines | ~150 lignes |
| `client/` (à identifier) | UUID selection + V1 init | ~50 lignes |
| `CLAUDE.md` | Note sur V1 support | Optionnel |

**Total : ~415 lignes** — travail d'une session.
