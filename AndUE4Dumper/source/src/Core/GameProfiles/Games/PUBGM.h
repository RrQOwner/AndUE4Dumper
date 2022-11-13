#pragma once

#include "../GameProfile.h"

// PUBGM
// UE 4.18

class PUBGMProfile : public IGameProfile
{
public:
    PUBGMProfile() = default;

    virtual bool ArchSupprted() const override
    {
        auto e_machine = GetBaseInfo().ehdr.e_machine;
        // arm & arm64
        return e_machine == EM_AARCH64 || e_machine == EM_ARM;
    }

    std::string GetAppName() const override
    {
        return "PUBG";
    }

    std::vector<std::string> GetAppIDs() const override
    {
        return {
            "com.tencent.ig",
            "com.rekoo.pubgm",
            "com.pubg.imobile",
            "com.pubg.krmobile",
            "com.vng.pubgmobile",  
        };

        // chinese version doesn't have GNames encrypted but FNameEntry* is encrypted
        // (game ver 1.20.13 arm64)
        // decrypt FNameEntry* -> sub_5158B18(__int64 in, __int64 *out)
        // GNames = 0x74F0480 -> name_index = 8 -> Name = 0xC
        // GUObjectArray = 0xB491A20 -> ObjObjects = 0xB0 -> NumElements = 0x38
    }

    bool IsUsingFNamePool() const override
    {
        return false;
    }

    uintptr_t GetGUObjectArrayPtr() const override
    {
        //return GetBaseInfo().map.startAddress + 0x0000000;

        auto e_machine = GetBaseInfo().ehdr.e_machine;
        // arm patterns
        if (e_machine == EM_ARM)
        {
            std::string hex = "BC109FE501108FE0082091E5";
            std::string mask(hex.length() / 2, 'x');
            uintptr_t insn_address = findPattern(PATTERN_MAP_TYPE::MAP_RXP, hex, mask, 0);
            if (insn_address != 0)
            {
                uintptr_t PC = insn_address + 8, PC_ldr = 0, R1 = 0, R2 = 4;

                PMemory::vm_rpm_ptr((void *)(insn_address), &PC_ldr, sizeof(uintptr_t));
                PC_ldr = KittyArm::decode_ldr_literal(PC_ldr);

                PMemory::vm_rpm_ptr((void *)(PC + PC_ldr), &R1, sizeof(uintptr_t));

                return (PC + R1 + R2);
            }

            // alternative .bss pattern
            insn_address = findPattern(PATTERN_MAP_TYPE::MAP_BSS, "010000000100F049020000000000", "xx??xxxxxxxxxx", -2);
            if (insn_address == 0)
            {
                LOGE("GUObjectArray pattern failed.");
                return 0;
            }
            return insn_address;
        }
        // arm64 patterns
        else if (e_machine == EM_AARCH64)
        {
            std::string hex = "12 40 B9 00 3E 40 B9 00 00 00 6B 00 00 00 54 00 00 00 00 00 00 00 91";
            std::string mask = "xxx?xxx???x???x???????x";
            int step = 0xF;

            uintptr_t insn_address = findPattern(PATTERN_MAP_TYPE::MAP_RXP, hex, mask, step);
            if(insn_address == 0)
            {
                LOGE("GUObjectArray pattern failed.");
                return 0;
            }

            int64_t adrp_pc_rel = 0;
            int32_t add_imm12 = 0;

            uintptr_t page_off = INSN_PAGE_OFFSET(insn_address);

            uint32_t adrp_insn = 0, add_insn = 0;
            PMemory::vm_rpm_ptr((void *)(insn_address), &adrp_insn, sizeof(uint32_t));
            PMemory::vm_rpm_ptr((void *)(insn_address + sizeof(uint32_t)), &add_insn, sizeof(uint32_t));

            if (adrp_insn == 0 || add_insn == 0)
                return 0;

            if (!KittyArm64::decode_adr_imm(adrp_insn, &adrp_pc_rel) || adrp_pc_rel == 0)
                return 0;

            add_imm12 = KittyArm64::decode_addsub_imm(add_insn);
            if (add_imm12 == 0)
                return 0;

            return (page_off + adrp_pc_rel + add_imm12);
        }

        return 0;
    }

    uintptr_t GetNamesPtr() const override
    {
        // uintptr_t enc_names = GetBaseInfo().map.startAddress + 0x0000000;
        uintptr_t enc_names = 0;

        auto e_machine = GetBaseInfo().ehdr.e_machine;
        // arm patterns
        if (e_machine == EM_ARM)
        {
            std::string hex = "E0019FE500008FE0307090E5";
            std::string mask(hex.length() / 2, 'x');
            uintptr_t insn_address = findPattern(PATTERN_MAP_TYPE::MAP_RXP, hex, mask, 0);
            if (insn_address != 0)
            {
                uintptr_t PC = insn_address + 0x8, PC_ldr = 0, R1 = 0, R2 = 0x30;

                PMemory::vm_rpm_ptr((void *)(insn_address), &PC_ldr, sizeof(uintptr_t));
                PC_ldr = KittyArm::decode_ldr_literal(PC_ldr);

                PMemory::vm_rpm_ptr((void *)(PC + PC_ldr), &R1, sizeof(uintptr_t));

                enc_names = (PC + R1 + R2 + 4);
            }
            else
            {
                // alternative .bss pattern
                hex = "00E432D8B00D4F891FB77ECFACA24AFD362843C6E1534D2CA2868E6CA38CBD1764";
                mask = std::string(hex.length() / 2, 'x');
                int step = -0xF;
                int skip = 1;
                enc_names = findPattern(PATTERN_MAP_TYPE::MAP_BSS, hex, mask, step, skip);
            }
        }
        // arm64 patterns
        else if (e_machine == EM_AARCH64)
        {
            std::string hex = "81 80 52 00 00 00 00 00 03 1F 2A";
            std::string mask = "xxx?????xxx";
            int step = 0x17;

            uintptr_t insn_address = findPattern(PATTERN_MAP_TYPE::MAP_RXP, hex, mask, step);
            if (insn_address != 0)
            {
                int64_t adrp_pc_rel = 0;
                int32_t add_imm12 = 0, ldrb_imm12 = 0;

                uintptr_t page_off = INSN_PAGE_OFFSET(insn_address);

                uint32_t adrp_insn = 0, add_insn = 0, ldrb_insn = 0;
                PMemory::vm_rpm_ptr((void *)(insn_address), &adrp_insn, sizeof(uint32_t));
                PMemory::vm_rpm_ptr((void *)(insn_address + 4), &add_insn, sizeof(uint32_t));
                PMemory::vm_rpm_ptr((void *)(insn_address + 8), &ldrb_insn, sizeof(uint32_t));

                if (adrp_insn == 0 || add_insn == 0 || ldrb_insn == 0)
                    return 0;

                if (!KittyArm64::decode_adr_imm(adrp_insn, &adrp_pc_rel) || adrp_pc_rel == 0)
                    return 0;

                add_imm12 = KittyArm64::decode_addsub_imm(add_insn);
                if (add_imm12 == 0)
                    return 0;

                if (!KittyArm64::decode_ldrstr_uimm(ldrb_insn, &ldrb_imm12) || ldrb_imm12 == 0)
                    return 0;

                enc_names = (page_off + adrp_pc_rel + add_imm12 + ldrb_imm12 - 4);
            }
        }

        if (enc_names == 0)
        {
            LOGE("GNames pattern failed.");
            return 0;
        }

        // getting randomized GNames ptr
        int32_t in;
        uintptr_t out[16];

        in = (PMemory::vm_rpm_ptr<int32_t>((void *)enc_names) - 100) / 3;
        out[in - 1] = PMemory::vm_rpm_ptr<uintptr_t>((void *)(enc_names + 8));

        while (in - 2 >= 0)
        {
            out[in - 2] = PMemory::vm_rpm_ptr<uintptr_t>((void *)(out[in - 1]));
            --in;
        }

        return PMemory::vm_rpm_ptr<uintptr_t>((void *)(out[0]));
    }

    UE_Offsets *GetOffsets() const override
    {
        // ===============  64bit offsets  =============== //
        struct
        {
            uint16 Stride = 0;          // not needed in versions older than UE4.23
            uint16 FNamePoolBlocks = 0; // not needed in versions older than UE4.23
            uint16 FNameMaxSize = 0xff;
            struct
            {
                uint16 Number = 4;
            } FName;
            struct
            {
                uint16 Name = 0xC;
            } FNameEntry;
            struct
            { // not needed in versions older than UE4.23
                uint16 Info = 0;
                uint16 WideBit = 0;
                uint16 LenBit = 0;
                uint16 HeaderSize = 0;
            } FNameEntry23;
            struct
            {
                uint16 ObjObjects = 0x10;
            } FUObjectArray;
            struct
            {
                uint16 NumElements = 0xC;
            } TUObjectArray;
            struct
            {
                uint16 Size = 0x18;
            } FUObjectItem;
            struct
            {
                uint16 ObjectFlags = 0x8;
                uint16 InternalIndex = 0xC;
                uint16 ClassPrivate = 0x10;
                uint16 NamePrivate = 0x18;
                uint16 OuterPrivate = 0x20;
            } UObject;
            struct
            {
                uint16 Next = 0x28; // sizeof(UObject)
            } UField;
            struct
            {
                uint16 SuperStruct = 0x30; // sizeof(UField)
                uint16 Children = 0x38;    // UField*
                uint16 ChildProperties = 0;  // not needed in versions older than UE4.25
                uint16 PropertiesSize = 0x40;
            } UStruct;
            struct
            {
                uint16 Names = 0x40; // usually at sizeof(UField) + sizeof(FString)
            } UEnum;
            struct
            {
                uint16 EFunctionFlags = 0x88; // sizeof(UStruct)
                uint16 NumParams = EFunctionFlags + 0x4;
                uint16 ParamSize = NumParams + 0x2;
                uint16 Func = EFunctionFlags + 0x28; // ue3-ue4, always +0x28 from flags location.
            } UFunction;
            struct
            { // not needed in versions older than UE4.25
                uint16 ClassPrivate = 0;
                uint16 Next = 0;
                uint16 NamePrivate = 0;
                uint16 FlagsPrivate = 0;
            } FField;
            struct
            { // not needed in versions older than UE4.25
                uint16 ArrayDim = 0;
                uint16 ElementSize = 0;
                uint16 PropertyFlags = 0;
                uint16 Offset_Internal = 0;
                uint16 Size = 0;
            } FProperty;
            struct
            {
                uint16 ArrayDim = 0x30; // sizeof(UField)
                uint16 ElementSize = 0x34;
                uint16 PropertyFlags = 0x38;
                uint16 Offset_Internal = 0x44;
                uint16 Size = 0x70; // sizeof(UProperty)
            } UProperty;
        } static profile64;
        static_assert(sizeof(profile64) == sizeof(UE_Offsets));

        // ===============  32bit offsets  =============== //
        struct
        {
            uint16 Stride = 0;          // not needed in versions older than UE4.23
            uint16 FNamePoolBlocks = 0; // not needed in versions older than UE4.23
            uint16 FNameMaxSize = 0xff;
            struct
            {
                uint16 Number = 4;
            } FName;
            struct
            {
                uint16 Name = 0x8;
            } FNameEntry;
            struct
            { // not needed in versions older than UE4.23
                uint16 Info = 0;
                uint16 WideBit = 0;
                uint16 LenBit = 0;
                uint16 HeaderSize = 0;
            } FNameEntry23;
            struct
            {
                uint16 ObjObjects = 0x10;
            } FUObjectArray;
            struct
            {
                uint16 NumElements = 0x8;
            } TUObjectArray;
            struct
            {
                uint16 Size = 0x10;
            } FUObjectItem;
            struct
            {
                uint16 ObjectFlags = 0x4;
                uint16 InternalIndex = 0x8;
                uint16 ClassPrivate = 0xC;
                uint16 NamePrivate = 0x10;
                uint16 OuterPrivate = 0x18;
            } UObject;
            struct
            {
                uint16 Next = 0x1C; // sizeof(UObject)
            } UField;
            struct
            {
                uint16 SuperStruct = 0x20; // sizeof(UField)
                uint16 Children = 0x24;    // UField*
                uint16 ChildProperties = 0;  // not needed in versions older than UE4.25
                uint16 PropertiesSize = 0x28;
            } UStruct;
            struct
            {
                uint16 Names = 0x2C; // usually at sizeof(UField) + sizeof(FString)
            } UEnum;
            struct
            {
                uint16 EFunctionFlags = 0x58; // sizeof(UStruct)
                uint16 NumParams = EFunctionFlags + 0x4;
                uint16 ParamSize = NumParams + 0x2;
                uint16 Func = EFunctionFlags + 0x1C; // +0x1C (32bit) from flags location.
            } UFunction;
            struct
            { // not needed in versions older than UE4.25
                uint16 ClassPrivate = 0;
                uint16 Next = 0;
                uint16 NamePrivate = 0;
                uint16 FlagsPrivate = 0;
            } FField;
            struct
            { // not needed in versions older than UE4.25
                uint16 ArrayDim = 0;
                uint16 ElementSize = 0;
                uint16 PropertyFlags = 0;
                uint16 Offset_Internal = 0;
                uint16 Size = 0;
            } FProperty;
            struct
            {
                uint16 ArrayDim = 0x20; // sizeof(UField)
                uint16 ElementSize = 0x24;
                uint16 PropertyFlags = 0x28;
                uint16 Offset_Internal = 0x34;
                uint16 Size = 0x50; // sizeof(UProperty)
            } UProperty;
        } static profile32;
        static_assert(sizeof(profile32) == sizeof(UE_Offsets));

#ifdef __LP64__
        return (UE_Offsets *)&profile64;
#else
        return (UE_Offsets *)&profile32;
#endif
    }
};