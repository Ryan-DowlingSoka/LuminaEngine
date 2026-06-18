#include <gtest/gtest.h>
#include <entt/entt.hpp>
#include "World/Entity/EntityUtils.h"
#include "World/Entity/Components/DirtyComponent.h"
#include "World/Entity/Components/RelationshipComponent.h"
#include "World/Entity/Components/TransformComponent.h"

#include <chrono>
#include <cstdio>

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

TEST(ECSTests, ResolveTransformChain_GrandchildFollowsRootMove)
{
    FEntityRegistry Registry{};

    auto A = Registry.create();
    auto B = Registry.create();
    auto C = Registry.create();

    Registry.emplace<STransformComponent>(A).LocalTransform.SetLocation(FVector3(10.f, 0.f, 0.f));
    Registry.emplace<STransformComponent>(B).LocalTransform.SetLocation(FVector3(5.f,  0.f, 0.f));
    Registry.emplace<STransformComponent>(C).LocalTransform.SetLocation(FVector3(2.f,  0.f, 0.f));

    // AddToParent only links the relationship; ReparentEntity would
    // bake the (zeroed) cached world matrix into local transforms.
    ECS::Utils::AddToParent(Registry, B, A);
    ECS::Utils::AddToParent(Registry, C, B);

    Registry.emplace<FNeedsTransformUpdate>(A);
    Registry.emplace<FNeedsTransformUpdate>(B);
    Registry.emplace<FNeedsTransformUpdate>(C);
    ECS::Utils::ResolveAllDirtyTransforms(Registry);

    EXPECT_FLOAT_EQ(Registry.get<STransformComponent>(C).WorldTransform.GetLocation().x, 17.f);

    Registry.get<STransformComponent>(A).LocalTransform.SetLocation(FVector3(20.f, 0.f, 0.f));
    Registry.emplace_or_replace<FNeedsTransformUpdate>(A);

    // Lazy resolve via the grandchild: A is dirty, B/C clean. ResolveTransformChain
    // must walk up to A and refresh the chain rather than serving C's stale matrix.
    ECS::Utils::ResolveTransformChain(Registry, C);

    const STransformComponent& WorldC = Registry.get<STransformComponent>(C);
    const STransformComponent& WorldB = Registry.get<STransformComponent>(B);
    const STransformComponent& WorldA = Registry.get<STransformComponent>(A);

    EXPECT_FLOAT_EQ(WorldA.WorldTransform.GetLocation().x, 20.f);
    EXPECT_FLOAT_EQ(WorldB.WorldTransform.GetLocation().x, 25.f);
    EXPECT_FLOAT_EQ(WorldC.WorldTransform.GetLocation().x, 27.f);
    EXPECT_FLOAT_EQ(WorldC.WorldTransform.GetLocation().x - WorldB.WorldTransform.GetLocation().x, 2.f);
}

TEST(ECSTests, ResolveTransformChain_SiblingSubtreeStaysConsistent)
{
    FEntityRegistry Registry{};

    auto A = Registry.create();
    auto B = Registry.create();
    auto C = Registry.create();
    auto D = Registry.create();

    Registry.emplace<STransformComponent>(A).LocalTransform.SetLocation(FVector3(10.f, 0.f, 0.f));
    Registry.emplace<STransformComponent>(B).LocalTransform.SetLocation(FVector3(5.f,  0.f, 0.f));
    Registry.emplace<STransformComponent>(C).LocalTransform.SetLocation(FVector3(2.f,  0.f, 0.f));
    Registry.emplace<STransformComponent>(D).LocalTransform.SetLocation(FVector3(0.f,  3.f, 0.f));

    ECS::Utils::AddToParent(Registry, B, A);
    ECS::Utils::AddToParent(Registry, C, B);
    ECS::Utils::AddToParent(Registry, D, A);

    Registry.emplace<FNeedsTransformUpdate>(A);
    Registry.emplace<FNeedsTransformUpdate>(B);
    Registry.emplace<FNeedsTransformUpdate>(C);
    Registry.emplace<FNeedsTransformUpdate>(D);
    ECS::Utils::ResolveAllDirtyTransforms(Registry);

    Registry.get<STransformComponent>(A).LocalTransform.SetLocation(FVector3(20.f, 0.f, 0.f));
    Registry.emplace_or_replace<FNeedsTransformUpdate>(A);

    // Resolving via C must also refresh sibling D, which isn't dirty itself and would
    // otherwise serve a stale matrix once C's resolve clears A's dirty bit.
    ECS::Utils::ResolveTransformChain(Registry, C);
    ECS::Utils::ResolveTransformChain(Registry, D);

    EXPECT_FLOAT_EQ(Registry.get<STransformComponent>(D).WorldTransform.GetLocation().x, 20.f);
    EXPECT_FLOAT_EQ(Registry.get<STransformComponent>(D).WorldTransform.GetLocation().y, 3.f);
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

// Guards the lock-free read fast path: after a clean phase (bAnyDirty=false), a gameplay setter marks
// the parent dirty via MarkTransformDirty -> the on_construct hook must re-arm the flag so the child's
// lazy read resolves the chain instead of serving a stale world transform.
TEST(ECSTests, LazyResolve_AfterCleanPhase_SeesUpdatedWorld)
{
    FEntityRegistry Registry{};

    entt::entity Parent = Registry.create();
    entt::entity Child  = Registry.create();
    Registry.emplace<STransformComponent>(Parent).LocalTransform.SetLocation(FVector3(10.f, 0.f, 0.f));
    Registry.emplace<STransformComponent>(Child).LocalTransform.SetLocation(FVector3(5.f, 0.f, 0.f));
    ECS::Utils::AddToParent(Registry, Child, Parent);

    Registry.get<STransformComponent>(Parent).Bind(Registry, Parent);
    Registry.get<STransformComponent>(Child).Bind(Registry, Child);

    // Clean phase: resolve everything, arming the lock-free fast path (bAnyDirty=false).
    Registry.emplace<FNeedsTransformUpdate>(Parent);
    Registry.emplace<FNeedsTransformUpdate>(Child);
    ECS::Utils::ResolveAllDirtyTransforms(Registry);
    EXPECT_FLOAT_EQ(Registry.get<STransformComponent>(Child).GetWorldLocation().x, 15.f);

    // Gameplay move through the normal setter -> MarkTransformDirty -> hook re-arms bAnyDirty.
    Registry.get<STransformComponent>(Parent).SetLocalLocation(FVector3(20.f, 0.f, 0.f));

    // The child isn't dirty itself; the read must still walk up, see the dirty parent, and resolve.
    EXPECT_FLOAT_EQ(Registry.get<STransformComponent>(Child).GetWorldLocation().x, 25.f);
}

// ============================================================================
// Benchmarks: extreme hierarchies. These don't assert on timing; they print
// timings + sanity-check correctness so we can profile and optimize.
// Run with: --gtest_filter='ECSBench.*'
// ============================================================================

namespace
{
    struct FBenchTimer
    {
        const char* Name;
        std::chrono::steady_clock::time_point Start;
        explicit FBenchTimer(const char* InName) : Name(InName), Start(std::chrono::steady_clock::now()) {}
        double Stop() const
        {
            const double Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
            std::printf("[BENCH] %-46s %10.3f ms\n", Name, Ms);
            std::fflush(stdout);
            return Ms;
        }
    };

    // NumRoots roots, each with ChildrenPerRoot children (single level). AddToParent only links the
    // relationship (ReparentEntity would bake the zeroed cached matrix into the local transform).
    void BuildWideForest(FEntityRegistry& Registry, int NumRoots, int ChildrenPerRoot, TVector<entt::entity>& OutRoots)
    {
        OutRoots.reserve(NumRoots);
        for (int r = 0; r < NumRoots; ++r)
        {
            entt::entity Root = Registry.create();
            Registry.emplace<STransformComponent>(Root).LocalTransform.SetLocation(FVector3((float)r, 0.f, 0.f));
            OutRoots.push_back(Root);
            for (int c = 0; c < ChildrenPerRoot; ++c)
            {
                entt::entity Child = Registry.create();
                Registry.emplace<STransformComponent>(Child).LocalTransform.SetLocation(FVector3(0.f, 1.f, 0.f));
                ECS::Utils::AddToParent(Registry, Child, Root);
            }
        }
    }

    // A single parent->child chain of the given depth. Returns the root.
    entt::entity BuildDeepChain(FEntityRegistry& Registry, int Depth)
    {
        entt::entity Prev = entt::null;
        entt::entity Root = entt::null;
        for (int d = 0; d < Depth; ++d)
        {
            entt::entity E = Registry.create();
            Registry.emplace<STransformComponent>(E).LocalTransform.SetLocation(FVector3(0.f, 1.f, 0.f));
            if (Prev != entt::null)
            {
                ECS::Utils::AddToParent(Registry, E, Prev);
            }
            else
            {
                Root = E;
            }
            Prev = E;
        }
        return Root;
    }
}

// One root, 100k children. Only the root is dirty: measures recursive descent + ForEachChild + the
// per-dirty-entity TFunction allocation in ResolveAllDirtyTransforms.
TEST(ECSBench, Resolve_Wide_SingleDirtyRoot)
{
    FEntityRegistry Registry{};
    TVector<entt::entity> Roots;
    {
        FBenchTimer Build("build wide(1 x 100k)");
        BuildWideForest(Registry, 1, 100000, Roots);
        Build.Stop();
    }
    Registry.emplace<FNeedsTransformUpdate>(Roots[0]);

    FBenchTimer T("resolve wide(1 x 100k) single dirty root");
    ECS::Utils::ResolveAllDirtyTransforms(Registry);
    const double Ms = T.Stop();
    (void)Ms;

    // child world = root(0,0,0) * local(0,1,0)
    auto& Rel = Registry.get<FRelationshipComponent>(Roots[0]);
    EXPECT_FLOAT_EQ(Registry.get<STransformComponent>(Rel.First).WorldTransform.GetLocation().y, 1.f);
}

// 1000 roots x 1000 children = ~1M entities; every root dirty (>1000 -> parallel path). The literal
// "thousands of entities with thousands of children" extreme: each root's 1000-child subtree is a
// disjoint descent fanned across workers.
TEST(ECSBench, Resolve_Forest_AllRootsDirty)
{
    FEntityRegistry Registry{};
    TVector<entt::entity> Roots;
    {
        FBenchTimer Build("build forest(1000 x 1000) ~1M");
        BuildWideForest(Registry, 1000, 1000, Roots);
        Build.Stop();
    }
    for (entt::entity R : Roots)
    {
        Registry.emplace<FNeedsTransformUpdate>(R);
    }

    FBenchTimer T("resolve forest(1000 x 1000) all roots dirty");
    ECS::Utils::ResolveAllDirtyTransforms(Registry);
    T.Stop();
}

// 2000 roots x 50 children = 102k entities, EVERY entity dirty (simulates a bulk spawn / initial load).
TEST(ECSBench, Resolve_Forest_AllEntitiesDirty)
{
    FEntityRegistry Registry{};
    TVector<entt::entity> Roots;
    {
        FBenchTimer Build("build forest(2k x 50)");
        BuildWideForest(Registry, 2000, 50, Roots);
        Build.Stop();
    }
    for (entt::entity E : Registry.view<STransformComponent>())
    {
        Registry.emplace<FNeedsTransformUpdate>(E);
    }

    FBenchTimer T("resolve forest(2k x 50) ALL entities dirty");
    ECS::Utils::ResolveAllDirtyTransforms(Registry);
    T.Stop();
}

// Deep chain. Descent is real recursion (UpdateChildrenRecursive) -- watch for stack growth.
TEST(ECSBench, Resolve_DeepChain)
{
    FEntityRegistry Registry{};
    entt::entity Root = entt::null;
    {
        FBenchTimer Build("build deep chain(50000)");
        Root = BuildDeepChain(Registry, 50000);
        Build.Stop();
    }
    Registry.emplace<FNeedsTransformUpdate>(Root);

    FBenchTimer T("resolve deep chain(50000) root dirty");
    ECS::Utils::ResolveAllDirtyTransforms(Registry);
    T.Stop();
}

// Lazy single-entity resolve via the leaf of a deep chain (ResolveTransformChain).
TEST(ECSBench, ResolveChain_DeepChain_ViaLeaf)
{
    FEntityRegistry Registry{};
    TVector<entt::entity> All;
    entt::entity Prev = entt::null;
    entt::entity Leaf = entt::null;
    {
        FBenchTimer Build("build deep chain(50000) [leaf]");
        for (int d = 0; d < 4000; ++d)
        {
            entt::entity E = Registry.create();
            Registry.emplace<STransformComponent>(E).LocalTransform.SetLocation(FVector3(0.f, 1.f, 0.f));
            if (Prev != entt::null)
            {
                ECS::Utils::AddToParent(Registry, E, Prev);
            }
            Prev = E;
            Leaf = E;
        }
        Build.Stop();
    }
    // Dirty the root only, then resolve through the leaf (must walk up + propagate down).
    entt::entity RootEntity = ECS::Utils::GetRootEntity(Registry, Leaf);
    Registry.emplace<FNeedsTransformUpdate>(RootEntity);

    FBenchTimer T("resolveChain deep(4000) via leaf");
    ECS::Utils::ResolveTransformChain(Registry, Leaf);
    T.Stop();
}

// Parallel reads of CLEAN world transforms. Every GetWorldLocation funnels through ResolveIfDirty ->
// ResolveTransformChain, which today takes a global exclusive mutex even when nothing is dirty -- so
// thousands of concurrent reads serialize. Measures that contention (parented entities, so the chain
// walk actually runs; resolved clean beforehand).
TEST(ECSBench, ParallelReads_CleanTransforms)
{
    FEntityRegistry Registry{};
    TVector<entt::entity> Children;
    constexpr int Roots = 2000;
    constexpr int PerRoot = 100; // 200k parented children
    {
        FBenchTimer Build("build forest(2000 x 100) + bind");
        for (int r = 0; r < Roots; ++r)
        {
            entt::entity Root = Registry.create();
            Registry.emplace<STransformComponent>(Root).LocalTransform.SetLocation(FVector3((float)r, 0.f, 0.f));
            for (int c = 0; c < PerRoot; ++c)
            {
                entt::entity Child = Registry.create();
                Registry.emplace<STransformComponent>(Child).LocalTransform.SetLocation(FVector3(0.f, 1.f, 0.f));
                ECS::Utils::AddToParent(Registry, Child, Root);
                Children.push_back(Child);
            }
        }
        // Bind self-pointers so GetWorldLocation -> ResolveIfDirty actually fires the chain resolve.
        for (entt::entity E : Registry.view<STransformComponent>())
        {
            Registry.get<STransformComponent>(E).Bind(Registry, E);
        }
        Build.Stop();
    }

    // Resolve everything once so all reads below hit the CLEAN path.
    for (entt::entity E : Registry.view<STransformComponent>())
    {
        Registry.emplace_or_replace<FNeedsTransformUpdate>(E);
    }
    ECS::Utils::ResolveAllDirtyTransforms(Registry);

    auto& Storage = Registry.storage<STransformComponent>();
    std::atomic<uint64> Sink{0};

    // Warm + serial baseline.
    {
        FBenchTimer T("serial   GetWorldLocation x200k (clean)");
        uint64 Acc = 0;
        for (entt::entity E : Children)
        {
            Acc += (uint64)Storage.get(E).GetWorldLocation().x;
        }
        Sink.fetch_add(Acc, std::memory_order_relaxed);
        T.Stop();
    }

    {
        FBenchTimer T("parallel GetWorldLocation x200k (clean)");
        Task::ParallelFor((uint32)Children.size(), [&](uint32 i)
        {
            const FVector3 L = Storage.get(Children[i]).GetWorldLocation();
            Sink.fetch_add((uint64)L.x, std::memory_order_relaxed);
        }, 256);
        T.Stop();
    }
    EXPECT_GE(Sink.load(), 0ull);
}

// The Rotator-script per-frame cost, decomposed. The script calls AddYaw (setter math + MarkDirty), then
// the frame boundary resolves + flushes physics bodies. Isolates each phase so we can see whether the
// regression is the SIMD math (it isn't), the MarkDirty enqueue, or the resolve. Flat entities (no parent,
// no physics body) -- exactly what a Rotator is -- so the DirtyBodies enqueue/flush is pure overhead here.
TEST(ECSBench, DirtyPath_RotatorFrameCost)
{
    using Clock = std::chrono::steady_clock;
    auto Ns = [](Clock::duration D) { return std::chrono::duration<double, std::nano>(D).count(); };

    FEntityRegistry Registry{};
    constexpr int N = 50000;
    constexpr int Frames = 200;
    const uint64 Ops = (uint64)N * Frames;

    TVector<entt::entity> Ents;
    Ents.reserve(N);
    for (int i = 0; i < N; ++i)
    {
        entt::entity E = Registry.create();
        Registry.emplace<STransformComponent>(E);
        Ents.push_back(E);
    }
    for (entt::entity E : Ents)
    {
        Registry.get<STransformComponent>(E).Bind(Registry, E);
    }

    auto& Storage = Registry.storage<STransformComponent>();
    volatile float Sink = 0.0f;

    auto Report = [&](const char* Name, double TotalNs)
    {
        std::printf("[BENCH] %-40s %8.2f ms  (%6.2f ns/op)\n", Name, TotalNs / 1e6, TotalNs / (double)Ops);
        std::fflush(stdout);
    };

    // A) Pure SIMD setter math: rotate the VTransform directly, no MarkDirty / no enqueue.
    double MathNs = 0.0;
    {
        auto T0 = Clock::now();
        for (int f = 0; f < Frames; ++f)
            for (entt::entity E : Ents)
                Storage.get(E).LocalTransform.AddYawRadians(0.01f);
        MathNs = Ns(Clock::now() - T0);
        Sink += Storage.get(Ents[0]).GetLocalRotation().w;
        Report("setter math only (AddYawRadians)", MathNs);
    }

    // B) Realistic frame: AddYaw (math + MarkDirty), then resolve, then physics-body flush -- timed apart.
    double DirtyNs = 0.0, ResolveNs = 0.0, FlushNs = 0.0;
    for (int f = 0; f < Frames; ++f)
    {
        auto T0 = Clock::now();
        for (entt::entity E : Ents) { Storage.get(E).AddYaw(0.5f); }
        auto T1 = Clock::now();
        ECS::Utils::ResolveAllDirtyTransforms(Registry);
        auto T2 = Clock::now();
        ECS::Utils::FlushDirtyPhysicsBodies(Registry);
        auto T3 = Clock::now();
        DirtyNs   += Ns(T1 - T0);
        ResolveNs += Ns(T2 - T1);
        FlushNs   += Ns(T3 - T2);
        Sink += Storage.get(Ents[0]).GetWorldRotationCached().w;
    }
    Report("AddYaw + MarkDirty (2-queue enqueue)", DirtyNs);
    Report("  -> MarkDirty/enqueue delta vs math", DirtyNs - MathNs);
    Report("ResolveAllDirtyTransforms", ResolveNs);
    Report("FlushDirtyPhysicsBodies (drain bodies)", FlushNs);

    EXPECT_TRUE(std::isfinite((float)Sink));
}