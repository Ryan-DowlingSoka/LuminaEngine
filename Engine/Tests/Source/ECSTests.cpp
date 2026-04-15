#include <gtest/gtest.h>
#include <entt/entt.hpp>
#include "World/Entity/EntityUtils.h"
#include "World/Entity/Components/RelationshipComponent.h"
#include "World/Entity/Components/TransformComponent.h"

using namespace Lumina;

TEST(ECSTests, Parent_SingleChild)
{
    entt::registry Registry{};

    auto Parent = Registry.create();
    Registry.emplace<STransformComponent>(Parent);

    auto Child = Registry.create();
    Registry.emplace<STransformComponent>(Child);
    ECS::Utils::ReparentEntity(Registry, Child, Parent);

    const auto& ParentRel = Registry.get<FRelationshipComponent>(Parent);
    const auto& ChildRel  = Registry.get<FRelationshipComponent>(Child);

    EXPECT_EQ(ParentRel.Parent, entt::entity{entt::null});
    EXPECT_EQ(ChildRel.Parent, Parent);

    EXPECT_EQ(ParentRel.First, Child);
    EXPECT_EQ(ParentRel.Children, 1);

    EXPECT_EQ(ChildRel.Prev, entt::entity{entt::null});
    EXPECT_EQ(ChildRel.Next, entt::entity{entt::null});
}

TEST(ECSTests, Parent_MultipleChildren_Order)
{
    FEntityRegistry Registry{};

    auto Parent = Registry.create();
    Registry.emplace<STransformComponent>(Parent);

    auto ChildA = Registry.create();
    auto ChildB = Registry.create();
    auto ChildC = Registry.create();

    Registry.emplace<STransformComponent>(ChildA);
    Registry.emplace<STransformComponent>(ChildB);
    Registry.emplace<STransformComponent>(ChildC);

    ECS::Utils::ReparentEntity(Registry, ChildA, Parent);
    ECS::Utils::ReparentEntity(Registry, ChildB, Parent);
    ECS::Utils::ReparentEntity(Registry, ChildC, Parent);

    const auto& ParentRel = Registry.get<FRelationshipComponent>(Parent);

    EXPECT_EQ(ParentRel.Children, 3);
    EXPECT_EQ(ParentRel.First, ChildC);

    const auto& A = Registry.get<FRelationshipComponent>(ChildA);
    const auto& B = Registry.get<FRelationshipComponent>(ChildB);
    const auto& C = Registry.get<FRelationshipComponent>(ChildC);

    EXPECT_EQ(A.Parent, Parent);
    EXPECT_EQ(B.Parent, Parent);
    EXPECT_EQ(C.Parent, Parent);

    EXPECT_EQ(C.Next, ChildB);
    EXPECT_EQ(B.Prev, ChildC);
    EXPECT_EQ(B.Next, ChildA);
    EXPECT_EQ(A.Prev, ChildB);
}

TEST(ECSTests, Parent_Reparent_MovesCorrectly)
{
    FEntityRegistry Registry{};

    auto ParentA = Registry.create();
    auto ParentB = Registry.create();
    Registry.emplace<STransformComponent>(ParentA);
    Registry.emplace<STransformComponent>(ParentB);

    auto Child = Registry.create();
    Registry.emplace<STransformComponent>(Child);

    ECS::Utils::ReparentEntity(Registry, Child, ParentA);
    ECS::Utils::ReparentEntity(Registry, Child, ParentB);

    const auto& A = Registry.get<FRelationshipComponent>(ParentA);
    const auto& B = Registry.get<FRelationshipComponent>(ParentB);
    const auto& C = Registry.get<FRelationshipComponent>(Child);

    EXPECT_EQ(C.Parent, ParentB);
    EXPECT_EQ(B.First, Child);
    EXPECT_EQ(B.Children, 1);

    EXPECT_EQ(A.Children, 0);
}

TEST(ECSTests, Parent_Unparent)
{
    FEntityRegistry Registry{};

    auto Parent = Registry.create();
    Registry.emplace<STransformComponent>(Parent);

    auto Child = Registry.create();
    Registry.emplace<STransformComponent>(Child);

    ECS::Utils::ReparentEntity(Registry, Child, Parent);
    ECS::Utils::ReparentEntity(Registry, Child, entt::null);

    const auto& ParentRel = Registry.get<FRelationshipComponent>(Parent);
    const auto& ChildRel  = Registry.get<FRelationshipComponent>(Child);

    EXPECT_EQ(ChildRel.Parent, entt::entity{entt::null});
    EXPECT_EQ(ParentRel.Children, 0);
    EXPECT_EQ(ParentRel.First, entt::entity{entt::null});
}