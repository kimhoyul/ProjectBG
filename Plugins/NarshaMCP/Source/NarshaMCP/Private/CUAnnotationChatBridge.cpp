// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5821: AI Annotation M7 — Bridge delegate global accessors

#include "CUAnnotationChatBridge.h"

FOnCUAnnotationDiscussRequested& GetOnCUAnnotationDiscussRequested()
{
	static FOnCUAnnotationDiscussRequested Delegate;
	return Delegate;
}

FOnCUChatAnnotationRequested& GetOnCUChatAnnotationRequested()
{
	static FOnCUChatAnnotationRequested Delegate;
	return Delegate;
}

FOnCUAnnotationHistoryQuery& GetOnCUAnnotationHistoryQuery()
{
	static FOnCUAnnotationHistoryQuery Delegate;
	return Delegate;
}
