#include "DX11Profiler.h"
#include "Timer.h"
#include "Profiler.h"

DX11Profiler DX11Profiler::sInstance;

//////////////////////////////////////////////////////////////////////////
void DX11Profiler::Initialize( ID3D11Device* device, ID3D11DeviceContext* immContext )
{
	mCurrFrame             = 0;
	mCurrDeepFrame         = 0;
	mTimeWaitingForQueries = 0;

	mDevice  = device;
	mContext = immContext;
}

//////////////////////////////////////////////////////////////////////////
void DX11Profiler::StartProfile(const wstring& name)
{
	if (Profiler::Instance()->GetState() == PROFILER_STATES::E_NONE)
		return;

	ProfileData& profileData = mProfiles[name];
	RJE_ASSERT(profileData.mQueryStarted  == FALSE);
	RJE_ASSERT(profileData.mQueryFinished == FALSE);

	if(profileData.mDisjointQuery[mCurrFrame] == nullptr)
	{
		// Create the queries
		D3D11_QUERY_DESC desc;
		desc.Query     = D3D11_QUERY_TIMESTAMP_DISJOINT;
		desc.MiscFlags = 0;
		RJE_CHECK_FOR_SUCCESS(mDevice->CreateQuery(&desc, &profileData.mDisjointQuery[mCurrFrame]));

		desc.Query = D3D11_QUERY_TIMESTAMP;
		RJE_CHECK_FOR_SUCCESS(mDevice->CreateQuery(&desc, &profileData.mTimestampStartQuery[mCurrFrame]));
		RJE_CHECK_FOR_SUCCESS(mDevice->CreateQuery(&desc, &profileData.mTimestampEndQuery[mCurrFrame]));
	}

	// Start a disjoint query first
	mContext->Begin(profileData.mDisjointQuery[mCurrFrame]);

	// Insert the start timestamp
	mContext->End(profileData.mTimestampStartQuery[mCurrFrame]);

	profileData.mQueryStarted = TRUE;
}

//////////////////////////////////////////////////////////////////////////
void DX11Profiler::StartDeepProfile(const wstring& name)
{
	if (Profiler::Instance()->GetState() != PROFILER_STATES::E_GPU)
		return;

	ProfileDataDeep& profileData = mDeepProfiles[name];
	RJE_ASSERT(profileData.mQueryStarted  == FALSE);
	RJE_ASSERT(profileData.mQueryFinished == FALSE);

	if (profileData.mPipelineStatsQuery[mCurrDeepFrame] == nullptr)
	{
		D3D11_QUERY_DESC pipelineStatsQueryDesc;
		pipelineStatsQueryDesc.Query     = D3D11_QUERY_PIPELINE_STATISTICS;
		pipelineStatsQueryDesc.MiscFlags = 0;
		RJE_CHECK_FOR_SUCCESS(mDevice->CreateQuery(&pipelineStatsQueryDesc, &profileData.mPipelineStatsQuery[mCurrDeepFrame]))
	}

	mContext->Begin(profileData.mPipelineStatsQuery[mCurrDeepFrame]);

	profileData.mQueryStarted = TRUE;
}

//////////////////////////////////////////////////////////////////////////
void DX11Profiler::EndProfile(const wstring& name)
{
	if (Profiler::Instance()->GetState() == PROFILER_STATES::E_NONE)
		return;

	ProfileData& profileData = mProfiles[name];
	RJE_ASSERT(profileData.mQueryStarted  == TRUE);
	RJE_ASSERT(profileData.mQueryFinished == FALSE);

	// Insert the end timestamp
	mContext->End(profileData.mTimestampEndQuery[mCurrFrame]);

	// End the disjoint query
	mContext->End(profileData.mDisjointQuery[mCurrFrame]);

	profileData.mQueryStarted  = FALSE;
	profileData.mQueryFinished = TRUE;
}

//////////////////////////////////////////////////////////////////////////
void DX11Profiler::EndDeepProfile(const wstring& name)
{
	if (Profiler::Instance()->GetState() != PROFILER_STATES::E_GPU)
		return;

	ProfileDataDeep& profileData = mDeepProfiles[name];
	RJE_ASSERT(profileData.mQueryStarted  == TRUE);
	RJE_ASSERT(profileData.mQueryFinished == FALSE);

	// Insert the end timestamp
	mContext->End(profileData.mPipelineStatsQuery[mCurrDeepFrame]);
	
	profileData.mQueryStarted  = FALSE;
	profileData.mQueryFinished = TRUE;
}

//////////////////////////////////////////////////////////////////////////
void DX11Profiler::EndFrame()
{
	if (Profiler::Instance()->GetState() == PROFILER_STATES::E_NONE)
	{
		mTimeWaitingForQueries = 0.0f;
		return;
	}

	float queryTime = 0.0f;

	// Iterate over all of the profiles
	ProfileMap::iterator it;
	for(it = mProfiles.begin(); it != mProfiles.end(); it++)
	{
		ProfileData& profile = (*it).second;
		if(profile.mQueryFinished == FALSE)
			continue;

		profile.mQueryFinished = FALSE;

		if(profile.mDisjointQuery[mCurrFrame] == nullptr)
			continue;

		// Get the query data
		u64 startTime = 0;
		RJE_WAIT_FOR_SUCCESS(mContext->GetData(profile.mTimestampStartQuery[mCurrFrame], &startTime, sizeof(startTime), 0));
		RJE_SAFE_RELEASE(profile.mTimestampStartQuery[mCurrFrame]);

		u64 endTime = 0;
		RJE_WAIT_FOR_SUCCESS(mContext->GetData(profile.mTimestampEndQuery[mCurrFrame], &endTime, sizeof(endTime), 0));
		RJE_SAFE_RELEASE(profile.mTimestampEndQuery[mCurrFrame]);

		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData;
		RJE_WAIT_FOR_SUCCESS(mContext->GetData(profile.mDisjointQuery[mCurrFrame], &disjointData, sizeof(disjointData), 0));
		RJE_SAFE_RELEASE(profile.mDisjointQuery[mCurrFrame]);

		queryTime += Timer::Instance()->RealDeltaTime();

		float time = 0.0f;
		if(disjointData.Disjoint == FALSE)
		{
			u64 delta = endTime - startTime;
			float frequency = static_cast<float>(disjointData.Frequency);
			time = (delta / frequency) * 1000.0f;
		}
		profile.mElaspedTime = time;
	}
	//---------------------------------
	// We check the statistics query only if the profiler is on GPU mode
	if (Profiler::Instance()->GetState() == PROFILER_STATES::E_GPU)
	{
		DeepProfileMap::iterator it2;
		for(it2 = mDeepProfiles.begin(); it2 != mDeepProfiles.end(); it2++)
		{
			ProfileDataDeep& profile = (*it2).second;
			if(profile.mQueryFinished == FALSE)
				continue;

			profile.mQueryFinished = FALSE;

			if(profile.mPipelineStatsQuery[mCurrDeepFrame] == nullptr)
				continue;

			D3D11_QUERY_DATA_PIPELINE_STATISTICS pipelineStatsData;
			RJE_WAIT_FOR_SUCCESS(mContext->GetData(profile.mPipelineStatsQuery[mCurrDeepFrame], &pipelineStatsData, sizeof(pipelineStatsData), 0));
			RJE_SAFE_RELEASE(profile.mPipelineStatsQuery[mCurrDeepFrame]);

			queryTime += Timer::Instance()->RealDeltaTime();

			profile.mInfos.Info_Rendering.IAVertices           = pipelineStatsData.IAVertices;
			profile.mInfos.Info_Rendering.IAPrimitives         = pipelineStatsData.IAPrimitives;
			profile.mInfos.Info_Rendering.VSInvoc              = pipelineStatsData.VSInvocations;
			profile.mInfos.Info_Rendering.PSInvoc              = pipelineStatsData.PSInvocations;
			profile.mInfos.Info_Rendering.GSInvoc              = pipelineStatsData.GSInvocations;
			profile.mInfos.Info_Rendering.HSInvoc              = pipelineStatsData.HSInvocations;
			profile.mInfos.Info_Rendering.DSInvoc              = pipelineStatsData.DSInvocations;
			profile.mInfos.Info_Rendering.CSInvoc              = pipelineStatsData.CSInvocations;
			profile.mInfos.Info_Rendering.rasterizerPrimitives = pipelineStatsData.CInvocations;
			profile.mInfos.Info_Rendering.renderedPrimitives   = pipelineStatsData.CPrimitives;
		}

		mCurrDeepFrame = (mCurrFrame + 1) % QueryLatency;
	}

	//=======================

	mTimeWaitingForQueries = queryTime;

	mCurrFrame = (mCurrFrame + 1) % QueryLatency;
}