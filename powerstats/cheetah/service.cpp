/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "android.hardware.power.stats-service.pixel"

#include <dataproviders/DisplayStateResidencyDataProvider.h>
#include <dataproviders/GenericStateResidencyDataProvider.h>
#include <dataproviders/PowerStatsEnergyConsumer.h>
#include <Gs201CommonDataProviders.h>
#include <PowerStatsAidl.h>

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <log/log.h>
#include <sys/stat.h>

using aidl::android::hardware::power::stats::DisplayStateResidencyDataProvider;
using aidl::android::hardware::power::stats::EnergyConsumerType;
using aidl::android::hardware::power::stats::GenericStateResidencyDataProvider;
using aidl::android::hardware::power::stats::PowerStatsEnergyConsumer;

void addDisplay(std::shared_ptr<PowerStats> p) {
    // Add display residency stats
    struct stat buffer;
    if (!stat("/sys/class/drm/card0/device/primary-panel/time_in_state", &buffer)) {
        // time_in_state exists
        addDisplayMrr(p);
    } else {
        // time_in_state doesn't exist
        std::vector<std::string> states = {
            "Off",
            "LP: 1440x3120@30",
            "On: 1440x3120@10",
            "On: 1440x3120@60",
            "On: 1440x3120@120",
            "HBM: 1440x3120@60",
            "HBM: 1440x3120@120",
            "LP: 1080x2340@30",
            "On: 1080x2340@10",
            "On: 1080x2340@60",
            "On: 1080x2340@120",
            "HBM: 1080x2340@60",
            "HBM: 1080x2340@120"};

        p->addStateResidencyDataProvider(std::make_unique<DisplayStateResidencyDataProvider>(
                "Display",
                "/sys/class/backlight/panel0-backlight/state",
                states));
    }

    // Add display energy consumer
    p->addEnergyConsumer(PowerStatsEnergyConsumer::createMeterAndEntityConsumer(
            p, EnergyConsumerType::DISPLAY, "display", {"VSYS_PWR_DISPLAY"}, "Display",
            {{"LP: 1440x3120@30", 1},
             {"On: 1440x3120@10", 2},
             {"On: 1440x3120@60", 3},
             {"On: 1440x3120@120", 4},
             {"HBM: 1440x3120@60", 5},
             {"HBM: 1440x3120@120", 6},
             {"LP: 1080x2340@30", 7},
             {"On: 1080x2340@10", 8},
             {"On: 1080x2340@60", 9},
             {"On: 1080x2340@120", 10},
             {"HBM: 1080x2340@60", 11},
             {"HBM: 1080x2340@120", 12}}));
}

void addUwb(std::shared_ptr<PowerStats> p) {
    // A constant to represent the number of nanoseconds in one millisecond.
    const int NS_TO_MS = 1000000;

    // ACPM stats are reported in nanoseconds. The transform function
    // converts nanoseconds to milliseconds.
    std::function<uint64_t(uint64_t)> uwbNsToMs = [](uint64_t a) { return a / NS_TO_MS; };
    const GenericStateResidencyDataProvider::StateResidencyConfig stateConfig = {
            .entryCountSupported = true,
            .entryCountPrefix = "count:",
            .totalTimeSupported = true,
            .totalTimePrefix = "dur ns:",
            .totalTimeTransform = uwbNsToMs,
            .lastEntrySupported = false,
    };

    const std::vector<std::pair<std::string, std::string>> stateHeaders = {
            std::make_pair("Off", "Off state:"),
            std::make_pair("Deep sleep", "Deep sleep state:"),
            std::make_pair("Run", "Run state:"),
            std::make_pair("Idle", "Idle state:"),
            std::make_pair("Tx", "Tx state:"),
            std::make_pair("Rx", "Rx state:"),
    };

    std::vector<GenericStateResidencyDataProvider::PowerEntityConfig> cfgs;
    cfgs.emplace_back(generateGenericStateResidencyConfigs(stateConfig, stateHeaders),
            "UWB", "");

    p->addStateResidencyDataProvider(std::make_unique<GenericStateResidencyDataProvider>(
            "/sys/devices/platform/10db0000.spi/spi_master/spi16/spi16.0/uwb/power_stats", cfgs));
}

int main() {
    struct stat buffer;

    LOG(INFO) << "Pixel PowerStats HAL AIDL Service is starting.";

    // single thread
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    std::shared_ptr<PowerStats> p = ndk::SharedRefBase::make<PowerStats>();

    addGs201CommonDataProviders(p);
    addDisplay(p);
    addUwb(p);

    if (!stat("/sys/devices/platform/10970000.hsi2c/i2c-8/8-0008/power_stats", &buffer)) {
        addNFC(p, "/sys/devices/platform/10970000.hsi2c/i2c-8/8-0008/power_stats");
    }
    const std::string instance = std::string() + PowerStats::descriptor + "/default";
    binder_status_t status = AServiceManager_addService(p->asBinder().get(), instance.c_str());
    LOG_ALWAYS_FATAL_IF(status != STATUS_OK);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
