# topas2mqtt

Anslutar till Topas reningsverk, samlar in data och postart det sedan till en MQTT-server. En del data översätts och postas i klartext. Det är fortfarande register som är oklart vad de gör.

# Register:

Register 1 verksnummer

Register 64 Pr1 <state>
Register 69 Pr2 <state>
Register 70 Pr3 <state>
Register 71 Pr4 <state>
Register 72 Pr5 <state>
state:
   1: Manually (Manuellt)
   2: UV-lamp (UV-lampa)
   3: Chemicals (Flockningsmedel)
   4: Back-up blower (Kompressordrift sekundär kompressor)
   5: Emergency accumulation level (E101) (Bräddningsnivå i utjämningstank)
   6: Programable timer 1 (Programerbar timer)
   7: Programable timer 2 (Programerbar timer)
   8: Programable timer 3 (Programerbar timer)
   9: Interval timer 1 (Intervalltimer)
   10: Interval timer 2 (Intervalltimer)
   11: Float ACU (Flyta i utjämningstank)
   12: Float in Acc. without emergency level (Float in Acc. without emergency level)
   13: Indication of water discharge (Indication of water discharge)
   14: Chemicals 2 (Flockningsmedel 2)

Register 213 FlowMeter (?)

Register 1006 - max aktivace tank level in cm (processtank?)
Register 1009 - max akumlace tank level in cm (akumulatortank)
Register 1027 - ? suggCapMultiplier (värde 0)
Register 1040 - ? chemie-hide (värde 300?)
Register 1044 - ? sand (värde 2)
Register 1048 - ? chemie2-hide (värde 0)

Register 10007 output/intput
1     - bit 1 - DM Kompressordrift
2     - bit 2 - Pr1
4     - bit 3 - V1o Utjämningstank
!4    - bit ^3 - V1c Procersstank
8     - bit 4 - V2o Tömning renat vatten
!8    - bit ^4 - V2c ?
16     - bit 5 - V3o Dekanterfyllning
!16    - bit ^5 - V3c ?
32     - bit 6 - V4o Avslammning
!32    - bit ^6 - V4c ?
64     - bit 7 - Pr2 Bräddningsnivå i utjämningstank
128    - bit 8 - Pr3 Bräddningsnivå i utjämningstank
256    - bit 9 - Pr4 UV-lampa
512    - bit 10 - Pr5 Flockningsmedel
1024   - bit 11 - Input D1
2048   - bit 12 - Input D2
4096   - bit 13 - Input D3
8192   - bit 14 - Input D4

Register 10058 GSM signal and state

Register 11000 - Current aktivace tank level in cm (processtank?)
Register 11001 - Current akumlace tank level in cm (akumulatortank)
Register 11002 - Current capacity in percent / 10 (100 = 10%)
Register 11003 - Current phase
   phase:
        0: Performing Activation (Fyllnings av processtank)
        1: Sedimentation (Sedimentering)
        2: Filling the decanter (Dekanterfyllning)
        3: Desalination (Avslamning)
        4: Draining (Tömning renat vatten)
        5: Denitri. fulfillment (Denitrifiering fyllning av processtank)
        6: Denitri. sedimentation (Denitrifiering sedimentering)
        7: Denitri. recirculation (Denitrifiering recirkulation)
        8: Triggering (Kör)

Register 11004 - Varaktighet i nuvarande fas (i minuter)
Register 11005 - Fastid fyllning av processtank (i minuter)
Register 11006 - Fastid sedimentering (i minuter)
Register 11007 - Fastid dekanterfyllning (i minuter)
Register 11008 - Fastid avslamning (i minuter)
Register 11009 - Fastid tömning renat vatten (i minuter)
Register 11010 - Fastid denitrifiering fyllning av processtank (i minuter)
Register 11011 - Fastid denitrifiering sedimentering (i minuter)
Register 11012 - Fastid denitrifiering recirklulation (i minuter)

Register 11025 - Amount cleared water today (in m3)

Register 11047 - Error state
  bit 1 - E107 - The desludging stage lasts longer than max time (Avslamningsfel processtank)
  bit 2 - E108 - Emergency water level in bio-reactor (Bräddnivå i processtank)
  bit 3 - E131 - Dosing container is nearly empty (Flockningsmedel snart slut)
  bit 4 - E133 - Dosing container 2 is nearlt empty (Flockningsmedel tank 2 snart slut)

Register 11048 - Error state
  bit 1 - E104 - Raw sewage water air-lift pump defect or increased wastewater inflow (Pumpfel råvattenpump)
  bit 2 - E105 - Long-term increased level in accumulation tank (Högt inflöde)
  bit 3 - E106 - Denitrification stage lasts longer than max time (Avslamningsfel vid denitrifikation)
  bit 4 - E130 - Dosing container is empty (Flockningsmedel behållare tom)
  bit 5 - R132 - Dosing continer 2 is empty (Flockningsmedel tank 2 behållare tom)
  bit 6 - E110 - Air pressure drop in ackumulator tank (Utjämningstank tryckfall)
  bit 7 - E111 - Air pressure drop in bio-reactor tank (Processtank tryckfall)
  bit 8 - E150 - Critical temperature of control unit (Avloppsreningsverket är överhettat)

Register 11049 - Error state
  bit 1 - E101 - Emergency water level in accumulation tank (Bräddningsnivå i utjämningstank)
  bit 2 - E102 - Air pressure drop (Tryckfall)
  bit 3 - E103 - The drainage state lasts longer than max time (Pumpfel renvattenpump)
  bit 14 - E003 (8192) - License not valid (Licensöverträdelse)
  bit 15 - E002 (16384) - Electricity power on (Ström återställt)
  bit 16 - E001 (32768) - Electricity power off (Strömavbrott)

Register 11051 - Mode
  Mode:
        0: Off
        1: Automatic
        2: Manual
        3: Service
