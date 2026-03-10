#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"

#include "Core/FlightFunctional.h"
#include "FlightDataSources.h"

namespace Flight::Data
{

inline Flight::Functional::TResult<UDataTable*, FString> ResolveTableAsset(
	const FFlightDataTableSource& Source,
	const TCHAR* SourceName)
{
	if (Source.SourceKind != EFlightDataSourceKind::DataTable)
	{
		return Flight::Functional::Err<UDataTable*>(FString::Printf(TEXT("%s uses unsupported source kind"), SourceName));
	}

	UDataTable* Table = Source.DataTable.LoadSynchronous();
	if (!Table)
	{
		return Flight::Functional::Err<UDataTable*>(FString::Printf(TEXT("%s not set in project settings"), SourceName));
	}

	return Flight::Functional::Ok(Table);
}

inline Flight::Functional::TResult<UDataTable*, FString> ResolveTableAsset(
	const FFlightDataRowSource& Source,
	const TCHAR* SourceName)
{
	FFlightDataTableSource TableSource;
	TableSource.SourceKind = Source.SourceKind;
	TableSource.DataTable = Source.DataTable;
	return ResolveTableAsset(TableSource, SourceName);
}

template<typename TRow>
Flight::Functional::TResult<TRow, FString> ResolveRow(
	const FFlightDataRowSource& Source,
	const TCHAR* SourceName,
	const TCHAR* Context)
{
	return ResolveTableAsset(Source, SourceName)
		.AndThen([&Source, SourceName, Context](UDataTable* Table) -> Flight::Functional::TResult<TRow, FString>
		{
			if (Source.RowName.IsNone())
			{
				return Flight::Functional::Err<TRow>(FString::Printf(TEXT("%s row name not specified"), SourceName));
			}

			const TRow* Row = Table->FindRow<TRow>(Source.RowName, Context);
			if (!Row)
			{
				return Flight::Functional::Err<TRow>(FString::Printf(TEXT("Row '%s' not found in %s"), *Source.RowName.ToString(), SourceName));
			}

			return Flight::Functional::Ok(*Row);
		});
}

template<typename TRow, typename FilterFn, typename OnRowFn>
Flight::Functional::TResult<TArray<TRow>, FString> ResolveRows(
	const FFlightDataTableSource& Source,
	const TCHAR* SourceName,
	FilterFn&& RowFilter,
	OnRowFn&& OnRow)
{
	return ResolveTableAsset(Source, SourceName)
		.AndThen([&RowFilter, &OnRow, SourceName](UDataTable* Table) -> Flight::Functional::TResult<TArray<TRow>, FString>
		{
			TArray<TRow> Result;

			for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
			{
				const TRow* Row = reinterpret_cast<const TRow*>(Pair.Value);
				if (!Row)
				{
					continue;
				}

				if (!RowFilter(*Row))
				{
					continue;
				}

				TRow RowCopy = *Row;
				OnRow(RowCopy, Pair.Key);
				Result.Add(MoveTemp(RowCopy));
			}

			if (Result.Num() == 0)
			{
				return Flight::Functional::Err<TArray<TRow>>(FString::Printf(TEXT("No rows matched filter in %s"), SourceName));
			}

			return Flight::Functional::Ok(MoveTemp(Result));
		});
}

inline bool ValidateSource(
	const FFlightDataRowSource& Source,
	const FString& SourceLabel,
	TArray<FString>& OutIssues)
{
	if (Source.SourceKind != EFlightDataSourceKind::DataTable)
	{
		OutIssues.Add(FString::Printf(TEXT("%s uses an unsupported source kind"), *SourceLabel));
		return false;
	}

	if (Source.DataTable.IsNull())
	{
		OutIssues.Add(FString::Printf(TEXT("%s is not configured"), *SourceLabel));
		return false;
	}

	if (!Source.DataTable.LoadSynchronous())
	{
		OutIssues.Add(FString::Printf(TEXT("%s failed to load"), *SourceLabel));
		return false;
	}

	return true;
}

inline bool ValidateSource(
	const FFlightDataTableSource& Source,
	const FString& SourceLabel,
	TArray<FString>& OutIssues,
	const bool bOptional = false)
{
	if (Source.SourceKind != EFlightDataSourceKind::DataTable)
	{
		OutIssues.Add(FString::Printf(TEXT("%s uses an unsupported source kind"), *SourceLabel));
		return false;
	}

	if (Source.DataTable.IsNull())
	{
		if (!bOptional)
		{
			OutIssues.Add(FString::Printf(TEXT("%s is not configured"), *SourceLabel));
		}
		return bOptional;
	}

	if (!Source.DataTable.LoadSynchronous())
	{
		OutIssues.Add(FString::Printf(TEXT("%s failed to load"), *SourceLabel));
		return false;
	}

	return true;
}

} // namespace Flight::Data
