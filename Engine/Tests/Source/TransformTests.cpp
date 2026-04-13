#include "Core/Math/Transform.h"
#include "GoogleTest/include/gtest/gtest.h"

TEST(TransformTests, DefaultTransformIsIdentity)
{
    Lumina::FTransform Transform{};

    EXPECT_FLOAT_EQ(Transform.Location.x, 0.0f);
    EXPECT_FLOAT_EQ(Transform.Location.y, 0.0f);
    EXPECT_FLOAT_EQ(Transform.Location.z, 0.0f);

    EXPECT_FLOAT_EQ(Transform.Scale.x, 1.0f);
    EXPECT_FLOAT_EQ(Transform.Scale.y, 1.0f);
    EXPECT_FLOAT_EQ(Transform.Scale.z, 1.0f);
}