#include "Globals.h"  // NOTE: MSVC stupidness requires this to be the same across all modules

#include "Player.h"
#include "ArrowEntity.h"
#include "../Chunk.h"





cArrowEntity::cArrowEntity(cEntity * a_Creator, double a_X, double a_Y, double a_Z, const Vector3d & a_Speed) :
	super(pkArrow, a_Creator, a_X, a_Y, a_Z, 0.5, 0.5),
	m_PickupState(psNoPickup),
	m_DamageCoeff(2),
	m_IsCritical(false),
	m_Timer(0),
	m_HitGroundTimer(0),
	m_bIsCollected(false),
	m_HitBlockPos(Vector3i(0, 0, 0))
{
	SetSpeed(a_Speed);
	SetMass(0.1);
	SetYawFromSpeed();
	SetPitchFromSpeed();
	LOGD("Created arrow %d with speed {%.02f, %.02f, %.02f} and rot {%.02f, %.02f}",
		 m_UniqueID, GetSpeedX(), GetSpeedY(), GetSpeedZ(),
		 GetYaw(), GetPitch()
		 );
}





cArrowEntity::cArrowEntity(cPlayer & a_Player, double a_Force) :
	super(pkArrow, &a_Player, a_Player.GetThrowStartPos(), a_Player.GetThrowSpeed(a_Force * 1.5 * 20), 0.5, 0.5),
	m_PickupState(psInSurvivalOrCreative),
	m_DamageCoeff(2),
	m_IsCritical((a_Force >= 1)),
	m_Timer(0),
	m_HitGroundTimer(0),
	m_HasTeleported(false),
	m_bIsCollected(false),
	m_HitBlockPos(0, 0, 0)
{
}





bool cArrowEntity::CanPickup(const cPlayer & a_Player) const
{
	switch (m_PickupState)
	{
		case psNoPickup:             return false;
		case psInSurvivalOrCreative: return (a_Player.IsGameModeSurvival() || a_Player.IsGameModeCreative());
		case psInCreative:           return a_Player.IsGameModeCreative();
	}
	ASSERT(!"Unhandled pickup state");
	return false;
}





void cArrowEntity::OnHitSolidBlock(const Vector3d & a_HitPos, eBlockFace a_HitFace)
{
	if (a_HitFace == BLOCK_FACE_NONE) { return; }
	
	super::OnHitSolidBlock(a_HitPos, a_HitFace);
	int a_X = (int)a_HitPos.x, a_Y = (int)a_HitPos.y, a_Z = (int)a_HitPos.z;
	
	switch (a_HitFace)
	{
		case BLOCK_FACE_XM: // Strangely, bounding boxes / block tracers return the actual block for these two directions, so AddFace not needed
		case BLOCK_FACE_YM:
		{
			break;
		}
		default: AddFaceDirection(a_X, a_Y, a_Z, a_HitFace, true);
	}
	
	m_HitBlockPos = Vector3i(a_X, a_Y, a_Z);
	
	// Broadcast arrow hit sound
	m_World->BroadcastSoundEffect("random.bowhit", (int)GetPosX() * 8, (int)GetPosY() * 8, (int)GetPosZ() * 8, 0.5f, (float)(0.75 + ((float)((GetUniqueID() * 23) % 32)) / 64));
}





void cArrowEntity::OnHitEntity(cEntity & a_EntityHit, const Vector3d & a_HitPos)
{
	if (!a_EntityHit.IsMob() && !a_EntityHit.IsMinecart() && !a_EntityHit.IsPlayer() && !a_EntityHit.IsBoat())
	{
		// Not an entity that interacts with an arrow
		return;
	}
	
	int Damage = (int)(GetSpeed().Length() / 20 * m_DamageCoeff + 0.5);
	if (m_IsCritical)
	{
		Damage += m_World->GetTickRandomNumber(Damage / 2 + 2);
	}
	a_EntityHit.TakeDamage(dtRangedAttack, this, Damage, 1);
	
	// Broadcast successful hit sound
	m_World->BroadcastSoundEffect("random.successful_hit", (int)GetPosX() * 8, (int)GetPosY() * 8, (int)GetPosZ() * 8, 0.5, (float)(0.75 + ((float)((GetUniqueID() * 23) % 32)) / 64));
	
	Destroy();
}





void cArrowEntity::CollectedBy(cPlayer * a_Dest)
{
	if ((m_IsInGround) && (!m_bIsCollected) && (CanPickup(*a_Dest)))
	{
		int NumAdded = a_Dest->GetInventory().AddItem(E_ITEM_ARROW);
		if (NumAdded > 0) // Only play effects if there was space in inventory
		{
			m_World->BroadcastCollectPickup((const cPickup &)*this, *a_Dest);
			// Also send the "pop" sound effect with a somewhat random pitch (fast-random using EntityID ;)
			m_World->BroadcastSoundEffect("random.pop", (int)GetPosX() * 8, (int)GetPosY() * 8, (int)GetPosZ() * 8, 0.5, (float)(0.75 + ((float)((GetUniqueID() * 23) % 32)) / 64));
			m_bIsCollected = true;
		}
	}
}





void cArrowEntity::Tick(float a_Dt, cChunk & a_Chunk)
{
	super::Tick(a_Dt, a_Chunk);
	m_Timer += a_Dt;
	
	if (m_bIsCollected)
	{
		if (m_Timer > 500.f)  // 0.5 seconds
		{
			Destroy();
			return;
		}
	}
	else if (m_Timer > 1000 * 60 * 5)  // 5 minutes
	{
		Destroy();
		return;
	}
	
	if (m_IsInGround)
	{
		// When an arrow hits, the client doesn't think its in the ground and keeps on moving, IF BroadcastMovementUpdate() and TeleportEntity was called during flight, AT ALL
		// Fix is to simply not sync with the client and send a teleport to confirm pos after arrow has stabilised (around 1 sec after landing)
		// We can afford to do this because xoft's algorithm for trajectory is near perfect, so things are pretty close anyway without sync
		// Besides, this seems to be what the vanilla server does, note how arrows teleport half a second after they hit to the server position
		
		if (!m_HasTeleported) // Sent a teleport already, don't do again
		{
			if (m_HitGroundTimer > 1000.f) // Send after a second, could be less, but just in case
			{
				m_World->BroadcastTeleportEntity(*this);
				m_HasTeleported = true;
			}
			else
			{
				m_HitGroundTimer += a_Dt;
			}
		}
		
		int RelPosX = m_HitBlockPos.x - a_Chunk.GetPosX() * cChunkDef::Width;
		int RelPosZ = m_HitBlockPos.z - a_Chunk.GetPosZ() * cChunkDef::Width;
		cChunk * Chunk = a_Chunk.GetRelNeighborChunkAdjustCoords(RelPosX, RelPosZ);
		
		if (Chunk == NULL)
		{
			// Inside an unloaded chunk, abort
			return;
		}
		
		if (Chunk->GetBlock(RelPosX, m_HitBlockPos.y, RelPosZ) == E_BLOCK_AIR) // Block attached to was destroyed?
		{
			m_IsInGround = false; // Yes, begin simulating physics again
		}
	}
}