using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace LuminaSharp;

/// <summary>
/// Owns the live EntityScript instances for one loaded generation. The link to native is a
/// <see cref="GCHandle"/> stored on the native SCSharpScriptComponent, every per-instance call
/// (ready/update/destroy/dispatch/apply) dereferences that handle directly, so there is NO per-handle
/// or per-world lookup dictionary. The only collection kept is a flat set of live handles, used solely
/// to free them before the ALC unloads (a strong GCHandle pins its target, so they must all be freed
/// or the collectible context can't unload). Reflection lives in the <see cref="TypeLibrary"/>.
///
/// Lifecycle is driven entirely by the native ECS view: native creates on attach, calls OnReady once
/// the whole world's batch is attached, batches OnUpdate into one crossing per world, and destroys on
/// detach. This type holds no per-world state.
/// </summary>
internal sealed class EntityScriptRuntime
{
    private readonly TypeLibrary Library;
    private readonly HashSet<GCHandle> LiveHandles = new();

    public EntityScriptRuntime(TypeLibrary Library)
    {
        this.Library = Library;
    }

    public IReadOnlyCollection<string> TypeNames => Library.EntityScriptTypeNames;

    /// <summary>Resolves an EntityScript type from this generation by full name (for the native dynamic
    /// invoker). Engine types resolve from LuminaSharp.dll directly; only script types come through here.</summary>
    public Type? FindType(string Name)
    {
        return Library.GetEntityScript(Name)?.Type;
    }

    /// <summary>Instantiates the named EntityScript for an entity, runs OnAttach, and returns a strong
    /// GCHandle (as IntPtr) the native component stores; IntPtr.Zero on failure.</summary>
    public IntPtr Create(string TypeName, ulong World, uint Entity)
    {
        TypeDescription? Description = Library.GetEntityScript(TypeName);
        if (Description == null)
        {
            Native.Log(ELogLevel.Warn, $"EntityScript type not found: '{TypeName}'.");
            return IntPtr.Zero;
        }

        if (Description.Create() is not EntityScript Script)
        {
            Native.Log(ELogLevel.Error, $"Failed to create EntityScript '{TypeName}'.");
            return IntPtr.Zero;
        }

        Script.Entity = new Entity(Entity);
        Script.World = new Lumina.CWorld(new IntPtr(unchecked((long)World)));

        try
        {
            using (Game.Push(Script.World, Script.Entity))
            {
                Script.OnAttach();
            }
        }
        catch (Exception Exception)
        {
            Native.Log(ELogLevel.Error, $"EntityScript.OnAttach threw: {Exception}");
        }

        GCHandle Handle = GCHandle.Alloc(Script);
        LiveHandles.Add(Handle);
        return GCHandle.ToIntPtr(Handle);
    }

    public void OnReady(IntPtr Handle)
    {
        if (Resolve(Handle) is not EntityScript Script)
        {
            return;
        }

        using (Game.Push(Script.World, Script.Entity))
        {
            try
            {
                Library.Describe(Script.GetType()).InjectRequiredComponents(Script);
            }
            catch (Exception Exception)
            {
                Native.Log(ELogLevel.Error, $"EntityScript [RequireComponent] injection threw: {Exception}");
            }

            try
            {
                Script.OnReady();
            }
            catch (Exception Exception)
            {
                Native.Log(ELogLevel.Error, $"EntityScript.OnReady threw: {Exception}");
            }
        }
    }

    /// <summary>One crossing per world per frame: OnUpdate every ready script via direct virtual
    /// dispatch over the handle array native hands us.</summary>
    public unsafe void Update(IntPtr* Handles, int Count, float DeltaTime)
    {
        bool Profiling = Profiler.Enabled;
        for (int Index = 0; Index < Count; Index++)
        {
            if (Resolve(Handles[Index]) is not EntityScript Script)
            {
                continue;
            }

            try
            {
                if (Profiling)
                {
                    Profiler.Begin(Script.GetType().Name);
                }
                try
                {
                    using (Game.Push(Script.World, Script.Entity))
                    {
                        Script.OnUpdate(DeltaTime);
                    }
                }
                finally
                {
                    if (Profiling)
                    {
                        Profiler.End();
                    }
                }
            }
            catch (Exception Exception)
            {
                Native.Log(ELogLevel.Error, $"EntityScript.OnUpdate threw: {Exception}");
            }
        }
    }

    /// <summary>Dispatches OnFixedUpdate to a batch of scripts (one crossing per fixed step per world). Called
    /// 0..N times per frame by the native fixed-update accumulator, before the physics step.</summary>
    public unsafe void FixedUpdate(IntPtr* Handles, int Count, float FixedDeltaTime)
    {
        bool Profiling = Profiler.Enabled;
        for (int Index = 0; Index < Count; Index++)
        {
            if (Resolve(Handles[Index]) is not EntityScript Script)
            {
                continue;
            }

            try
            {
                if (Profiling)
                {
                    Profiler.Begin(Script.GetType().Name + ".FixedUpdate");
                }
                try
                {
                    using (Game.Push(Script.World, Script.Entity))
                    {
                        Script.OnFixedUpdate(FixedDeltaTime);
                    }
                }
                finally
                {
                    if (Profiling)
                    {
                        Profiler.End();
                    }
                }
            }
            catch (Exception Exception)
            {
                Native.Log(ELogLevel.Error, $"EntityScript.OnFixedUpdate threw: {Exception}");
            }
        }
    }

    public void Destroy(IntPtr Pointer)
    {
        if (Pointer == IntPtr.Zero)
        {
            return;
        }

        GCHandle Handle = GCHandle.FromIntPtr(Pointer);
        if (Handle.Target is EntityScript Script)
        {
            try
            {
                using (Game.Push(Script.World, Script.Entity))
                {
                    Script.OnDetach();
                }
            }
            catch (Exception Exception)
            {
                Native.Log(ELogLevel.Error, $"EntityScript.OnDetach threw: {Exception}");
            }
            Script.CancelDestroyToken();
        }

        LiveHandles.Remove(Handle);
        if (Handle.IsAllocated)
        {
            Handle.Free();
        }
    }

    // Collision + activation dispatch. Kind: 0=ContactBegin, 1=ContactEnd, 2=OverlapBegin, 3=OverlapEnd,
    // 5=Wake, 6=Sleep (the bit (1<<kind) must match the native callback-flag check; 4 is OnInput). The
    // activation kinds ignore Event (the body, not a contact, is the subject).
    public void Dispatch(IntPtr Handle, int Kind, in Lumina.SCollisionEvent Event)
    {
        if (Resolve(Handle) is not EntityScript Script)
        {
            return;
        }

        try
        {
            using var Scope = Game.Push(Script.World, Script.Entity);
            switch (Kind)
            {
                case 0:
                {
                    Script.OnContactBegin(Event);
                    break;
                }
                case 1:
                {
                    Script.OnContactEnd(Event);
                    break;
                }
                case 2:
                {
                    Script.OnOverlapBegin(Event);
                    break;
                }
                case 3:
                {
                    Script.OnOverlapEnd(Event);
                    break;
                }
                case 5:
                {
                    Script.OnWake();
                    break;
                }
                case 6:
                {
                    Script.OnSleep();
                    break;
                }
            }
        }
        catch (Exception Exception)
        {
            Native.Log(ELogLevel.Error, $"EntityScript collision callback threw: {Exception}");
        }
    }

    // AI perception dispatch. Kind: 7=OnTargetPerceived, 8=OnTargetLost (the bit (1<<kind) must match the
    // native callback-flag check in SPerceptionSystem). The payload is the self-oriented SPerceptionEvent.
    public void DispatchPerception(IntPtr Handle, int Kind, in Lumina.SPerceptionEvent Event)
    {
        if (Resolve(Handle) is not EntityScript Script)
        {
            return;
        }

        try
        {
            using var Scope = Game.Push(Script.World, Script.Entity);
            switch (Kind)
            {
                case 7:
                {
                    Script.OnTargetPerceived(Event);
                    break;
                }
                case 8:
                {
                    Script.OnTargetLost(Event);
                    break;
                }
            }
        }
        catch (Exception Exception)
        {
            Native.Log(ELogLevel.Error, $"EntityScript perception callback threw: {Exception}");
        }
    }

    /// <summary>Delivers one discrete input event to a script's OnInput (event-driven input listening).</summary>
    public void DispatchInput(IntPtr Handle, in Lumina.InputEvent Event)
    {
        if (Resolve(Handle) is not EntityScript Script)
        {
            return;
        }

        try
        {
            using var Scope = Game.Push(Script.World, Script.Entity);
            Script.OnInput(Event);
        }
        catch (Exception Exception)
        {
            Native.Log(ELogLevel.Error, $"EntityScript.OnInput threw: {Exception}");
        }
    }

    public int CallbackFlags(IntPtr Handle)
    {
        if (Resolve(Handle) is not EntityScript Script)
        {
            return 0;
        }
        return Library.Describe(Script.GetType()).CollisionCallbackFlags;
    }

    public unsafe void ApplyProperties(IntPtr Handle, byte* Blob, int Length)
    {
        if (Resolve(Handle) is not EntityScript Script)
        {
            return;
        }
        TypeDescription Description = Library.Describe(Script.GetType());
        Serializer.ApplyValues(Script, Description.Properties, Blob, Length);
    }

    public byte[]? Schema(string TypeName)
    {
        TypeDescription? Description = Library.GetEntityScript(TypeName);
        return Description != null ? Serializer.WriteSchema(Description) : null;
    }

    public byte[]? Buttons(string TypeName)
    {
        TypeDescription? Description = Library.GetEntityScript(TypeName);
        return Description != null ? Serializer.WriteButtons(Description) : null;
    }

    /// <summary>Frees every live handle (running OnDetach) so the collectible ALC can unload. Called on
    /// reload/shutdown before the old generation's context is torn down.</summary>
    public void FreeAll()
    {
        foreach (GCHandle Handle in LiveHandles)
        {
            if (Handle.Target is EntityScript Script)
            {
                try
                {
                    using (Game.Push(Script.World, Script.Entity))
                    {
                        Script.OnDetach();
                    }
                }
                catch (Exception Exception)
                {
                    Native.Log(ELogLevel.Error, $"EntityScript.OnDetach threw during unload: {Exception}");
                }
                Script.CancelDestroyToken();
            }
            if (Handle.IsAllocated)
            {
                Handle.Free();
            }
        }
        LiveHandles.Clear();
    }

    private static EntityScript? Resolve(IntPtr Pointer)
    {
        if (Pointer == IntPtr.Zero)
        {
            return null;
        }
        return GCHandle.FromIntPtr(Pointer).Target as EntityScript;
    }
}
