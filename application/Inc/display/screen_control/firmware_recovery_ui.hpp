#pragma once

enum class FirmwareRecoveryPage : unsigned char { Details, Confirm, Checking };
enum class FirmwareRecoveryAction { None, Start, Cancel };

__attribute__((optimize("Os"))) inline FirmwareRecoveryAction FirmwareRecovery_Input(FirmwareRecoveryPage& page, bool click, bool back) {
    if (back) {
        const bool cancel = page == FirmwareRecoveryPage::Checking;
        page = FirmwareRecoveryPage::Details;
        return cancel ? FirmwareRecoveryAction::Cancel : FirmwareRecoveryAction::None;
    }
    if (click && page == FirmwareRecoveryPage::Details) page = FirmwareRecoveryPage::Confirm;
    else if (click && page == FirmwareRecoveryPage::Confirm) {
        page = FirmwareRecoveryPage::Checking;
        return FirmwareRecoveryAction::Start;
    }
    return FirmwareRecoveryAction::None;
}
