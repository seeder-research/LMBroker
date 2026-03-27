#include <gtest/gtest.h>
#include "pool/lmutil_wrapper.h"

// ── Fixtures ──────────────────────────────────────────────────────────────────

static const char* kHealthyOutput = R"(
lmutil - Copyright (c) 1989-2023 Flexera. All Rights Reserved.
Flexible License Manager status on Thu 3/27/2026 10:00

[Detecting lmgrd processes...]
License server status: 27000@licserver1

Vendor daemon status (on licserver1):
    MLM: UP v11.19.4

Users of MATLAB:  (Total of 50 licenses issued;  12 licenses in use)

    "MATLAB" v24.0, vendor: MLM, expiry: permanent
    user1 workstation1 /dev/pts/0 (v24.0) (licserver1/27000 5302), start Thu 3/27 9:00

Users of Simulink:  (Total of 20 licenses issued;  0 licenses in use)

Users of DENIEDFEATURE:  (Total of 5 licenses issued;  5 licenses in use)
    (3 licenses queued)

Users of UNCOUNTED_FEAT:  (Uncounted)
)";

static const char* kServerDownOutput = R"(
lmutil - Copyright (c) 1989-2023 Flexera. All Rights Reserved.
lmstat: Cannot connect to license server system.
The server (lmgrd) has not been started yet, the wrong port@host or license
file is being specified, or the port or hostname in the license file has been
changed.
Feature:       MATLAB
License path:  27000@licserver1;
)";

static const char* kFlexErrorOutput = R"(
lmutil - Copyright (c) 1989-2023 Flexera. All Rights Reserved.
License server status: 27000@licserver1
lmstat: Error -96,287: License server machine is down or not responding.
)";

static const char* kMultiVendorOutput = R"(
Vendor daemon status (on licserver1):
    MLM: UP v11.19.4
    ADSKFLEX: UP v11.19.4

Users of MATLAB:  (Total of 10 licenses issued;  2 licenses in use)

    "MATLAB" v24.0, vendor: MLM

Users of AutoCAD:  (Total of 8 licenses issued;  3 licenses in use)

    "AutoCAD" v2024.0, vendor: ADSKFLEX
)";

static const char* kSplitLineOutput = R"(
Users of LONG_FEATURE_NAME:  (Total of 100 licenses
 issued;  50 licenses in use)
)";

// ── server_up detection ───────────────────────────────────────────────────────

TEST(LmutilParser, HealthyOutputServerUp) {
    auto r = pool::LmutilWrapper::parse_lmstat(kHealthyOutput);
    EXPECT_TRUE(r.server_up);
    EXPECT_TRUE(r.error_msg.empty());
}

TEST(LmutilParser, ServerDownOutput) {
    auto r = pool::LmutilWrapper::parse_lmstat(kServerDownOutput);
    EXPECT_FALSE(r.server_up);
    EXPECT_FALSE(r.error_msg.empty());
    EXPECT_TRUE(r.features.empty());
}

TEST(LmutilParser, FlexErrorOutput) {
    auto r = pool::LmutilWrapper::parse_lmstat(kFlexErrorOutput);
    EXPECT_FALSE(r.server_up);
}

TEST(LmutilParser, EmptyOutputNotServerUp) {
    auto r = pool::LmutilWrapper::parse_lmstat("");
    EXPECT_FALSE(r.server_up);
    EXPECT_FALSE(r.error_msg.empty());
}

// ── feature counts ────────────────────────────────────────────────────────────

TEST(LmutilParser, ParsesCorrectFeatureCount) {
    auto r = pool::LmutilWrapper::parse_lmstat(kHealthyOutput);
    ASSERT_TRUE(r.server_up);
    // MATLAB, Simulink, DENIEDFEATURE, UNCOUNTED_FEAT
    EXPECT_EQ(r.features.size(), 4u);
}

TEST(LmutilParser, MatlabCounts) {
    auto r = pool::LmutilWrapper::parse_lmstat(kHealthyOutput);
    auto it = std::find_if(r.features.begin(), r.features.end(),
        [](const pool::FeatureCount& f){ return f.feature == "MATLAB"; });
    ASSERT_NE(it, r.features.end());
    EXPECT_EQ(it->total,     50);
    EXPECT_EQ(it->in_use,    12);
    EXPECT_EQ(it->available, 38);
    EXPECT_EQ(it->queued,     0);
    EXPECT_FALSE(it->uncounted);
}

TEST(LmutilParser, ZeroInUse) {
    auto r = pool::LmutilWrapper::parse_lmstat(kHealthyOutput);
    auto it = std::find_if(r.features.begin(), r.features.end(),
        [](const pool::FeatureCount& f){ return f.feature == "Simulink"; });
    ASSERT_NE(it, r.features.end());
    EXPECT_EQ(it->available, 20);
    EXPECT_EQ(it->in_use,     0);
}

TEST(LmutilParser, FullyUsedWithQueue) {
    auto r = pool::LmutilWrapper::parse_lmstat(kHealthyOutput);
    auto it = std::find_if(r.features.begin(), r.features.end(),
        [](const pool::FeatureCount& f){ return f.feature == "DENIEDFEATURE"; });
    ASSERT_NE(it, r.features.end());
    EXPECT_EQ(it->available, 0);
    EXPECT_EQ(it->queued,    3);
}

TEST(LmutilParser, UncountedFeature) {
    auto r = pool::LmutilWrapper::parse_lmstat(kHealthyOutput);
    auto it = std::find_if(r.features.begin(), r.features.end(),
        [](const pool::FeatureCount& f){ return f.feature == "UNCOUNTED_FEAT"; });
    ASSERT_NE(it, r.features.end());
    EXPECT_TRUE(it->uncounted);
    EXPECT_EQ(it->total,    -1);
    EXPECT_EQ(it->available,-1);
}

// ── vendor tracking ───────────────────────────────────────────────────────────

TEST(LmutilParser, VendorAssignedFromDaemonSection) {
    auto r = pool::LmutilWrapper::parse_lmstat(kHealthyOutput);
    auto it = std::find_if(r.features.begin(), r.features.end(),
        [](const pool::FeatureCount& f){ return f.feature == "MATLAB"; });
    ASSERT_NE(it, r.features.end());
    EXPECT_EQ(it->vendor, "MLM");
}

TEST(LmutilParser, MultiVendorCorrectAssignment) {
    auto r = pool::LmutilWrapper::parse_lmstat(kMultiVendorOutput);
    ASSERT_TRUE(r.server_up);
    ASSERT_EQ(r.features.size(), 2u);

    auto matlab = std::find_if(r.features.begin(), r.features.end(),
        [](const pool::FeatureCount& f){ return f.feature == "MATLAB"; });
    ASSERT_NE(matlab, r.features.end());
    EXPECT_EQ(matlab->vendor, "MLM");

    auto acad = std::find_if(r.features.begin(), r.features.end(),
        [](const pool::FeatureCount& f){ return f.feature == "AutoCAD"; });
    ASSERT_NE(acad, r.features.end());
    EXPECT_EQ(acad->vendor, "ADSKFLEX");
}

// ── edge cases ────────────────────────────────────────────────────────────────

TEST(LmutilParser, SplitLineAcrossTwoLines) {
    auto r = pool::LmutilWrapper::parse_lmstat(kSplitLineOutput);
    ASSERT_TRUE(r.server_up);
    ASSERT_EQ(r.features.size(), 1u);
    EXPECT_EQ(r.features[0].feature,   "LONG_FEATURE_NAME");
    EXPECT_EQ(r.features[0].total,     100);
    EXPECT_EQ(r.features[0].in_use,     50);
    EXPECT_EQ(r.features[0].available,  50);
}

TEST(LmutilParser, IsServerDownHelperMatchesKnownStrings) {
    EXPECT_TRUE(pool::LmutilWrapper::is_server_down(
        "lmstat: Cannot connect to license server system."));
    EXPECT_TRUE(pool::LmutilWrapper::is_server_down(
        "License server machine is down or not responding."));
    EXPECT_TRUE(pool::LmutilWrapper::is_server_down("error code -96, something"));
    EXPECT_FALSE(pool::LmutilWrapper::is_server_down(
        "Users of MATLAB:  (Total of 10 licenses issued;  0 licenses in use)"));
}
