#include <gtest/gtest.h>
#include "pool/lmutil_wrapper.h"

// Sample lmstat -a output (trimmed to relevant lines)
static const char* kSampleOutput = R"(
lmutil - Copyright (c) 1989-2023 Flexera. All Rights Reserved.
Flexible License Manager status on Thu 3/27/2026 10:00

[Detecting lmgrd processes...]
License server status: 27000@licserver1
...

Users of MATLAB:  (Total of 50 licenses issued;  12 licenses in use)

  "MATLAB" v24.0, vendor: MLM, expiry: permanent
    user1 workstation1 /dev/pts/0 (v24.0) (licserver1/27000 5302), start Thu 3/27 9:00

Users of Simulink:  (Total of 20 licenses issued;  0 licenses in use)

Users of DENIEDFEATURE:  (Total of 5 licenses issued;  5 licenses in use)
)";

TEST(LmutilParser, ParsesMultipleFeatures) {
    auto features = pool::LmutilWrapper::parse_lmstat(kSampleOutput);
    ASSERT_EQ(features.size(), 3u);
}

TEST(LmutilParser, CorrectCountsForMatlab) {
    auto features = pool::LmutilWrapper::parse_lmstat(kSampleOutput);
    auto it = std::find_if(features.begin(), features.end(),
        [](const pool::FeatureCount& f){ return f.feature == "MATLAB"; });
    ASSERT_NE(it, features.end());
    EXPECT_EQ(it->total,     50);
    EXPECT_EQ(it->in_use,    12);
    EXPECT_EQ(it->available, 38);
}

TEST(LmutilParser, ZeroInUseFeature) {
    auto features = pool::LmutilWrapper::parse_lmstat(kSampleOutput);
    auto it = std::find_if(features.begin(), features.end(),
        [](const pool::FeatureCount& f){ return f.feature == "Simulink"; });
    ASSERT_NE(it, features.end());
    EXPECT_EQ(it->available, 20);
}

TEST(LmutilParser, FullyUsedFeature) {
    auto features = pool::LmutilWrapper::parse_lmstat(kSampleOutput);
    auto it = std::find_if(features.begin(), features.end(),
        [](const pool::FeatureCount& f){ return f.feature == "DENIEDFEATURE"; });
    ASSERT_NE(it, features.end());
    EXPECT_EQ(it->available, 0);
}

TEST(LmutilParser, EmptyOutputReturnsEmpty) {
    auto features = pool::LmutilWrapper::parse_lmstat("");
    EXPECT_TRUE(features.empty());
}
