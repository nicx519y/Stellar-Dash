from pathlib import Path
import shutil
import tempfile
import unittest
from .application_paths import ROOT, application_include_flags, run_native


class TxIspStateTests(unittest.TestCase):
    def test_runtime_and_screen_with_fake_hardware(self):
        # Execute the real dispatcher, ISP state, screen entry and menu builder.
        # Only peripheral/other-state owners are mocked; no device connection.
        with tempfile.TemporaryDirectory(prefix='xora-tx-isp-') as directory:
            temp = Path(directory)
            for subdir in ('states', 'screen_control', 'adc_btns'):
                (temp/subdir).mkdir()
            for source, target in (
                ('system/main_state_machine.hpp', 'main_state_machine.hpp'),
                ('system/main_runtime_control.hpp', 'main_runtime_control.hpp'),
                ('support/enums.hpp', 'enums.hpp'),
                ('system/states/base_state.hpp', 'states/base_state.hpp'),
                ('system/states/tx_isp_state.hpp', 'states/tx_isp_state.hpp'),
                ('display/screen_control/spi_screen_layout.hpp', 'screen_control/spi_screen_layout.hpp'),
                ('display/screen_control/spi_screen_main_list.hpp', 'screen_control/spi_screen_main_list.hpp'),
                ('display/screen_control/spi_screen_detail_entries.hpp', 'screen_control/spi_screen_detail_entries.hpp'),
            ):
                shutil.copyfile(ROOT/'application/Inc'/source, temp/target)
            shutil.copyfile(ROOT/'tools/tests/tx_isp_runtime_stubs.hpp', temp/'tx_isp_runtime_stubs.hpp')
            # Compile the production boot-to-page selector in isolation from LCD
            # hardware, including the pre-enter window during screen setup.
            screen=(ROOT/'application/Src/display/screen_control/spi_screen_manager.cpp').read_text(encoding='utf-8')
            selector=screen.split('static bool boot_mode_to_detail_menu',1)[1].split('void SPIScreenManager::setup',1)[0]
            (temp/'screen_boot_selector.cpp').write_text(
                '#include "tx_isp_runtime_stubs.hpp"\n#include "main_runtime_control.hpp"\n'
                '#include "screen_control/spi_screen_main_list.hpp"\nbool boot_mode_to_detail_menu'+selector,
                encoding='utf-8')
            stubs = ('storagemanager.hpp', 'config.hpp', 'board_cfg.h', 'board_mode.hpp',
                     'board_power.hpp', 'ch585_firmware_update.hpp', 'release_installer.hpp',
                     'ch585_role_bootstrap.hpp', 'connection_manager.hpp', 'power_manager.hpp',
                     'system_logger.h', 'system_sleep_manager.hpp', 'boot_profile.h',
                     'usb_board_link.hpp', 'usb_board_link_port.hpp', 'rf_bridge_port.hpp',
                     'stm32h7xx_hal.h', 'st7789.h', 'adc_btns/adc_manager.hpp',
                     'states/input_state.hpp', 'states/webconfig_state.hpp', 'states/calibration_state.hpp',
                     'states/ch585_bridge_update_state.hpp', 'states/safe_recovery_state.hpp',
                     'screen_control/spi_screen_manager.hpp', 'screen_control/spi_screen_ui_common.hpp',
                     'screen_control/spi_screen_detail_render_helpers.hpp')
            for name in stubs:
                # st7789 is included inside extern C: leave its wrapper empty.
                (temp/name).write_text('#pragma once\n' + ('' if name == 'st7789.h' else '#include "tx_isp_runtime_stubs.hpp"\n'), encoding='utf-8')
            exe = temp/'tx-isp-test.exe'
            compiler = shutil.which('g++')
            self.assertIsNotNone(compiler)
            run_native([compiler, '-std=c++17', '-Wall', '-Wextra', '-fmax-errors=5', '-I'+str(temp),
                        '-include', str(temp/'tx_isp_runtime_stubs.hpp'),
                        *application_include_flags(),
                        str(ROOT/'tools/tests/tx_isp_runtime_test.cpp'),
                        str(ROOT/'application/Src/system/main_state_machine.cpp'),
                        str(ROOT/'application/Src/system/states/tx_isp_state.cpp'),
                        str(ROOT/'application/Src/display/screen_control/spi_screen_detail_tx_isp.cpp'),
                        str(ROOT/'application/Src/display/screen_control/spi_screen_main_list.cpp'),
                        str(temp/'screen_boot_selector.cpp'),
                        '-o', str(exe)], check=True)
            run_native([str(exe)], check=True)

    def test_shared_runtime_and_screen_keep_isp_owned_by_manual_exit(self):
        main = (ROOT/'application/Src/system/main_state_machine.cpp').read_text(encoding='utf-8')
        shared = main.split('void MainStateMachine::serviceSharedRuntime()', 1)[1].split('void MainStateMachine::setup()', 1)[0]
        self.assertIn('currentState == MainRuntimeState::Input ||', shared)
        self.assertIn('currentState == MainRuntimeState::WebConfig', shared)
        self.assertIn('SystemSleep_Service(currentState == MainRuntimeState::Input', shared)
        self.assertIn('interactiveRuntimeInitialized && currentState != MainRuntimeState::TxIsp', main)
        self.assertLess(main.index('serviceSharedRuntime();'), main.index('servicePendingTransition();'))
        screen = (ROOT/'application/Src/display/screen_control/spi_screen_manager.cpp').read_text(encoding='utf-8')
        forced = screen.split('static bool boot_mode_to_detail_menu', 1)[1].split('void SPIScreenManager::setup', 1)[0]
        self.assertLess(forced.index('BOOT_MODE_TX_ISP'), forced.index('BOOT_MODE_WEB_CONFIG'))
        pages = (ROOT/'application/Src/display/screen_control/spi_screen_detail_pages.cpp').read_text(encoding='utf-8')
        self.assertIn('case SCREEN_MENU_TX_ISP: return ScreenDetailTxIsp_OnBack();', pages)
        self.assertIn('case SCREEN_MENU_TX_ISP: return ScreenDetailTxIsp_OnConfirm(index);', pages)
        self.assertIn('case SCREEN_MENU_TX_ISP: ScreenDetailTxIsp_Render', pages)


if __name__ == '__main__':
    unittest.main()
