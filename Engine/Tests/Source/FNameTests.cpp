#include <gtest/gtest.h>
#include "Containers/Name.h"

using namespace Lumina;

TEST(FNameTests, NameNone)
{
    EXPECT_TRUE(FName{}.IsNone());
    EXPECT_TRUE(FName(NAME_None).IsNone());
}

TEST(FNameTests, ConstructFromCString)
{
    FName a("Test");
    FName b("Test");

    EXPECT_EQ(a, b);
    EXPECT_FALSE(a.IsNone());
    EXPECT_NE(a.GetID(), 0u);
}

TEST(FNameTests, SameStringProducesSameID)
{
    FName a("Engine");
    FName b("Engine");

    EXPECT_EQ(a.GetID(), b.GetID());
    EXPECT_EQ(a, b);
}

TEST(FNameTests, DifferentStringsProduceDifferentIDs)
{
    FName a("Engine");
    FName b("Renderer");

    EXPECT_NE(a.GetID(), b.GetID());
    EXPECT_NE(a, b);
}

TEST(FNameTests, ConstructFromStringView)
{
    FStringView view("Physics");
    FName a(view);

    EXPECT_FALSE(a.IsNone());
    EXPECT_NE(a.GetID(), 0u);
    EXPECT_EQ(a.ToString(), "Physics");
}

TEST(FNameTests, ConstructFromStdString)
{
    FString str = "Audio";
    FName a(str);

    EXPECT_EQ(a.ToString(), "Audio");
}

TEST(FNameTests, ConstructFromWideString)
{
    FWString wstr = StringUtils::ToWideString("Render");
    FName a(wstr);

    EXPECT_EQ(a.ToString(), "Render");
}

TEST(FNameTests, ConstructFromTCHAR)
{
    const TCHAR* w = StringUtils::ToWideString("Input").c_str();
    FName a(w);

    EXPECT_EQ(a.ToString(), "Input");
}

TEST(FNameTests, CopyConstructorAndAssignment)
{
    FName a("Core");
    FName b = a;
    FName c;
    c = a;

    EXPECT_EQ(a, b);
    EXPECT_EQ(a, c);
}

TEST(FNameTests, EqualityOperators)
{
    FName a("Mesh");
    FName b("Mesh");
    FName c("Material");

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
}

TEST(FNameTests, ComparisonOperators)
{
    FName a("A");
    FName b("B");

    EXPECT_TRUE((a < b) || (a > b));
    EXPECT_NE(a, b);
}

TEST(FNameTests, EnumConversion)
{
    FName a(NAME_None);

    EXPECT_TRUE(a == NAME_None);
    EXPECT_TRUE(a.IsNone());
}

TEST(FNameTests, UInt64Constructor)
{
    FName a("Test");
    uint64 id = a.GetID();

    FName b(id);

    EXPECT_EQ(a, b);
}

TEST(FNameTests, DereferenceOperatorReturnsString)
{
    FName a("Shader");

    const char* str = *a;

    EXPECT_NE(str, nullptr);
    EXPECT_STREQ(str, "Shader");
}

TEST(FNameTests, LengthConsistency)
{
    FName a("Transform");

    EXPECT_EQ(a.Length(), strlen(a.c_str()));
    EXPECT_EQ(a.length(), a.Length());
}

TEST(FNameTests, HashStability)
{
    FName a("StableName");
    FName b("StableName");

    EXPECT_EQ(a.hash(), b.hash());
}

TEST(FNameTests, AtAccessValid)
{
    FName a("ABC");

    EXPECT_EQ(a.At(0), 'A');
    EXPECT_EQ(a.At(1), 'B');
    EXPECT_EQ(a.At(2), 'C');
}

TEST(FNameTests, ToStringRoundTrip)
{
    FName a("RoundTrip");

    FString s = a.ToString();
    FName b(s);

    EXPECT_EQ(a, b);
}