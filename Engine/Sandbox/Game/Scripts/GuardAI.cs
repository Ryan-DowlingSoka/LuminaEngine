using System;
using LuminaSharp;
using Lumina;

namespace Game;

/// <summary>
/// A guard that chases whatever its AI perception senses. Attach a "C# Script" component with this script to
/// an entity that also has a navmesh-walkable <c>SCharacterControllerComponent</c>. The thing it should
/// notice needs an <c>SAIStimuliSourceComponent</c> tagged with <see cref="DetectTeam"/> (author it in the
/// editor, or call <c>World.Perception.RegisterAsSource(player, GameplayTag.Request("Team.Player"))</c>).
///
/// Toggle the console var <c>ai.Perception.Debug 1</c> to see the sight cone, ranges, and target lines.
/// </summary>
public sealed class GuardAI : EntityScript
{
    [Property(Tooltip = "Affiliation tag this guard hunts; only sources carrying it are sensed. Empty = sense everyone.")]
    public string DetectTeam = "Team.Player";

    [Property(Tooltip = "Movement speed throttle (0..1) when chasing or investigating.")]
    public float ChaseSpeed = 1.0f;

    public override void OnReady()
    {
        // Ensure a perception component and restrict it to the chosen team. Skip this and author the
        // SPerceptionComponent + DetectableTags in the editor instead if you prefer.
        if (!string.IsNullOrEmpty(DetectTeam))
        {
            World.Perception.AddDetectableTag(Entity, GameplayTag.Request(DetectTeam));
        }

        // Characters don't rotate by default, which would freeze the sight cone facing one direction. Turn to
        // face the movement direction so the cone tracks whatever we chase (and we can re-acquire it).
        Lumina.SCharacterMovementComponent? Movement = Registry.TryGet<Lumina.SCharacterMovementComponent>(Entity);
        if (Movement != null)
        {
            Movement.bOrientRotationToMovement = true;
        }
    }

    public override void OnTargetPerceived(SPerceptionEvent Event)
    {
        Debug.Log($"Guard {Entity.Id} perceived {Event.Target.Id} via {Event.Sense}. Giving chase.");
        World.Navigation.Follow(Entity, Event.Target, ChaseSpeed);
    }

    public override void OnTargetLost(SPerceptionEvent Event)
    {
        // Walk to where the target was last sensed to investigate, then idle once it arrives.
        Debug.Log($"Guard {Entity.Id} lost {Event.Target.Id}. Investigating last known location.");
        World.Navigation.StopMoving(Entity);
        World.Navigation.MoveTo(Entity, Event.Location, ChaseSpeed);
    }
}
