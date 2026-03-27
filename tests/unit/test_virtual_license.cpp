#include <gtest/gtest.h>
#include "broker/virtual_license.h"

TEST(VirtualLicense, RenderContainsServerLine) {
    broker::VirtualLicense vl;
    vl.broker_host = "mybroker";
    vl.broker_port = 27000;
    auto text = vl.render();
    EXPECT_NE(text.find("SERVER mybroker"), std::string::npos);
    EXPECT_NE(text.find("27000"), std::string::npos);
}

TEST(VirtualLicense, RenderContainsFeatureLines) {
    broker::VirtualLicense vl;
    vl.broker_host = "mybroker";
    vl.broker_port = 27000;
    vl.features.push_back({"MATLAB", "MLM", "24.0", 50});
    vl.features.push_back({"Simulink", "MLM", "24.0", 20});
    auto text = vl.render();
    EXPECT_NE(text.find("FEATURE MATLAB"),   std::string::npos);
    EXPECT_NE(text.find("FEATURE Simulink"), std::string::npos);
}

TEST(VirtualLicense, NoFeaturesRendersServerLineOnly) {
    broker::VirtualLicense vl;
    vl.broker_host = "h";
    vl.broker_port = 1234;
    auto text = vl.render();
    EXPECT_NE(text.find("SERVER h"), std::string::npos);
    EXPECT_EQ(text.find("FEATURE"),  std::string::npos);
}
