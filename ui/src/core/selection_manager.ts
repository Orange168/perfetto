// Copyright (C) 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

import {assertTrue, assertUnreachable} from '../base/logging';
import {
  Selection,
  LegacySelection,
  Area,
  SelectionOpts,
  SelectionManager,
  AreaSelectionAggregator,
  SqlSelectionResolver,
} from '../public/selection';
import {duration, Time, time, TimeSpan} from '../base/time';
import {
  GenericSliceDetailsTabConfig,
  GenericSliceDetailsTabConfigBase,
} from '../public/details_panel';
import {raf} from './raf_scheduler';
import {exists, Optional} from '../base/utils';
import {TrackManagerImpl} from './track_manager';
import {Engine} from '../trace_processor/engine';
import {ScrollHelper} from './scroll_helper';
import {NoteManagerImpl} from './note_manager';
import {SearchResult} from '../public/search';
import {SelectionAggregationManager} from './selection_aggregation_manager';

const INSTANT_FOCUS_DURATION = 1n;
const INCOMPLETE_SLICE_DURATION = 30_000n;

// There are two selection-related states in this class.
// 1. _selection: This is the "input" / locator of the selection, what other
//    parts of the codebase specify (e.g., a tuple of trackUri + eventId) to say
//    "please select this object if it exists".
// 2. _selected{Slice,ThreadState}: This is the resolved selection, that is, the
//    rich details about the object that has been selected. If the input
//    `_selection` is valid, this is filled in the near future. Doing so
//    requires querying the SQL engine, which is an async operation.
export class SelectionManagerImpl implements SelectionManager {
  private _selection: Selection = {kind: 'empty'};
  private _selectedDetails?: LegacySelectionDetails;
  private _aggregationManager: SelectionAggregationManager;
  // Incremented every time _selection changes.
  private readonly selectionResolvers = new Array<SqlSelectionResolver>();

  constructor(
    engine: Engine,
    private trackManager: TrackManagerImpl,
    private noteManager: NoteManagerImpl,
    private scrollHelper: ScrollHelper,
    private onSelectionChange: (s: Selection, opts: SelectionOpts) => void,
  ) {
    this._aggregationManager = new SelectionAggregationManager(
      engine.getProxy('SelectionAggregationManager'),
    );
  }

  registerAreaSelectionAggreagtor(aggr: AreaSelectionAggregator): void {
    this._aggregationManager.registerAggregator(aggr);
  }

  clear(): void {
    this.setSelection({kind: 'empty'});
  }

  async selectTrackEvent(
    trackUri: string,
    eventId: number,
    opts?: SelectionOpts,
  ) {
    const details = await this.trackManager
      .getTrack(trackUri)
      ?.track.getSelectionDetails?.(eventId);

    if (!details) {
      throw new Error('Unable to resolve selection details');
    }

    this.setSelection(
      {
        ...details,
        kind: 'track_event',
        trackUri,
        eventId,
      },
      opts,
    );
  }

  selectNote(args: {id: string}, opts?: SelectionOpts) {
    this.setSelection(
      {
        kind: 'note',
        id: args.id,
      },
      opts,
    );
  }

  selectArea(area: Area, opts?: SelectionOpts): void {
    const {start, end} = area;
    assertTrue(start <= end);

    // In the case of area selection, the caller provides a list of trackUris.
    // However, all the consumer want to access the resolved TrackDescriptor.
    // Rather than delegating this to the various consumers, we resolve them
    // now once and for all and place them in the selection object.
    const tracks = [];
    for (const uri of area.trackUris) {
      const trackDescr = this.trackManager.getTrack(uri);
      if (trackDescr === undefined) continue;
      tracks.push(trackDescr);
    }

    this.setSelection(
      {
        kind: 'area',
        tracks,
        ...area,
      },
      opts,
    );
  }

  toggleTrackAreaSelection(trackUri: string) {
    const curSelection = this._selection;
    if (curSelection.kind !== 'area') return;

    let trackUris = curSelection.trackUris.slice();
    if (!trackUris.includes(trackUri)) {
      trackUris.push(trackUri);
    } else {
      trackUris = trackUris.filter((t) => t !== trackUri);
    }
    this.setSelection({
      ...curSelection,
      trackUris,
    });
  }

  toggleGroupAreaSelection(trackUris: string[]) {
    const curSelection = this._selection;
    if (curSelection.kind !== 'area') return;

    const allTracksSelected = trackUris.every((t) =>
      curSelection.trackUris.includes(t),
    );

    let newTrackUris: string[];
    if (allTracksSelected) {
      // Deselect all tracks in the list
      newTrackUris = curSelection.trackUris.filter(
        (t) => !trackUris.includes(t),
      );
    } else {
      newTrackUris = curSelection.trackUris.slice();
      trackUris.forEach((t) => {
        if (!newTrackUris.includes(t)) {
          newTrackUris.push(t);
        }
      });
    }
    this.setSelection({
      ...curSelection,
      trackUris: newTrackUris,
    });
  }

  // There is no matching addLegacy as we did not support multi-single
  // selection with the legacy selection system.
  selectLegacy(legacySelection: LegacySelection, opts?: SelectionOpts): void {
    this.setSelection(
      {
        kind: 'legacy',
        legacySelection,
      },
      opts,
    );
  }

  selectGenericSlice(args: {
    id: number;
    sqlTableName: string;
    start: time;
    duration: duration;
    trackUri: string;
    detailsPanelConfig: {
      kind: string;
      config: GenericSliceDetailsTabConfigBase;
    };
  }): void {
    const detailsPanelConfig: GenericSliceDetailsTabConfig = {
      id: args.id,
      ...args.detailsPanelConfig.config,
    };
    this.setSelection({
      kind: 'legacy',
      legacySelection: {
        kind: 'GENERIC_SLICE',
        id: args.id,
        sqlTableName: args.sqlTableName,
        start: args.start,
        duration: args.duration,
        trackUri: args.trackUri,
        detailsPanelConfig: {
          kind: args.detailsPanelConfig.kind,
          config: detailsPanelConfig,
        },
      },
    });
  }

  get selection(): Selection {
    return this._selection;
  }

  get legacySelection(): LegacySelection | null {
    return toLegacySelection(this._selection);
  }

  get legacySelectionDetails(): LegacySelectionDetails | undefined {
    return this._selectedDetails;
  }

  registerSqlSelectionResolver(resolver: SqlSelectionResolver): void {
    this.selectionResolvers.push(resolver);
  }

  async resolveSqlEvent(
    sqlTableName: string,
    id: number,
  ): Promise<{eventId: number; trackUri: string} | undefined> {
    const matchingResolvers = this.selectionResolvers.filter(
      (r) => r.sqlTableName === sqlTableName,
    );

    for (const resolver of matchingResolvers) {
      const result = await resolver.callback(id, sqlTableName);
      if (result) {
        // If we have multiple resolvers for the same table, just return the first one.
        return result;
      }
    }

    return undefined;
  }

  selectSqlEvent(sqlTableName: string, id: number, opts?: SelectionOpts): void {
    this.resolveSqlEvent(sqlTableName, id).then((selection) => {
      selection &&
        this.selectTrackEvent(selection.trackUri, selection.eventId, opts);
    });
  }

  private setSelection(selection: Selection, opts?: SelectionOpts) {
    this._selection = selection;

    this.onSelectionChange(selection, opts ?? {});

    raf.scheduleFullRedraw();

    if (opts?.scrollToSelection) {
      this.scrollToCurrentSelection();
    }

    if (this._selection.kind === 'area') {
      this._aggregationManager.aggregateArea(this._selection);
    } else {
      this._aggregationManager.clear();
    }
  }

  selectSearchResult(searchResult: SearchResult) {
    const {source, eventId, trackUri} = searchResult;
    if (eventId === undefined) {
      return;
    }
    switch (source) {
      case 'track':
        this.scrollHelper.scrollTo({
          track: {uri: trackUri, expandGroup: true},
        });
        break;
      case 'cpu':
        this.selectSqlEvent('sched_slice', eventId, {
          clearSearch: false,
          scrollToSelection: true,
          switchToCurrentSelectionTab: true,
        });
        break;
      case 'log':
        this.selectLegacy(
          {
            kind: 'LOG',
            id: eventId,
            trackUri,
          },
          {
            clearSearch: false,
            switchToCurrentSelectionTab: true,
          },
        );
        break;
      case 'slice':
        // Search results only include slices from the slice table for now.
        // When we include annotations we need to pass the correct table.
        this.selectSqlEvent('slice', eventId, {
          clearSearch: false,
          scrollToSelection: true,
          switchToCurrentSelectionTab: true,
        });
        break;
      default:
        assertUnreachable(source);
    }
  }

  scrollToCurrentSelection() {
    const uri = (() => {
      switch (this.selection.kind) {
        case 'track_event':
          return this.selection.trackUri;
        case 'legacy':
          return this.selection.legacySelection.trackUri;
        default:
          return undefined;
      }
    })();
    const range = this.findTimeRangeOfSelection();
    this.scrollHelper.scrollTo({
      time: range ? {...range} : undefined,
      track: uri ? {uri: uri, expandGroup: true} : undefined,
    });
  }

  findTimeRangeOfSelection(): Optional<TimeSpan> {
    const sel = this.selection;
    if (sel.kind === 'area') {
      return new TimeSpan(sel.start, sel.end);
    } else if (sel.kind === 'note') {
      const selectedNote = this.noteManager.getNote(sel.id);
      if (selectedNote !== undefined) {
        const kind = selectedNote.noteType;
        switch (kind) {
          case 'SPAN':
            return new TimeSpan(selectedNote.start, selectedNote.end);
          case 'DEFAULT':
            return TimeSpan.fromTimeAndDuration(
              selectedNote.timestamp,
              INSTANT_FOCUS_DURATION,
            );
          default:
            assertUnreachable(kind);
        }
      }
    } else if (sel.kind === 'track_event') {
      return TimeSpan.fromTimeAndDuration(sel.ts, sel.dur);
    }

    const legacySel = this.legacySelection;
    if (!exists(legacySel)) {
      return undefined;
    }

    if (legacySel.kind === 'LOG') {
      // TODO(hjd): Make focus selection work for logs.
    } else if (legacySel.kind === 'GENERIC_SLICE') {
      return findTimeRangeOfSlice({
        ts: legacySel.start,
        dur: legacySel.duration,
      });
    }

    return undefined;
  }

  get aggregation() {
    return this._aggregationManager;
  }
}

function toLegacySelection(selection: Selection): LegacySelection | null {
  switch (selection.kind) {
    case 'area':
    case 'track_event':
    case 'empty':
    case 'note':
      return null;
    case 'union':
      for (const child of selection.selections) {
        const result = toLegacySelection(child);
        if (result !== null) {
          return result;
        }
      }
      return null;
    case 'legacy':
      return selection.legacySelection;
    default:
      assertUnreachable(selection);
      return null;
  }
}

// Returns the start and end points of a slice-like object If slice is instant
// or incomplete, dummy time will be returned which instead.
function findTimeRangeOfSlice(slice: {ts?: time; dur?: duration}): TimeSpan {
  if (exists(slice.ts) && exists(slice.dur)) {
    if (slice.dur === -1n) {
      return TimeSpan.fromTimeAndDuration(slice.ts, INCOMPLETE_SLICE_DURATION);
    } else if (slice.dur === 0n) {
      return TimeSpan.fromTimeAndDuration(slice.ts, INSTANT_FOCUS_DURATION);
    } else {
      return TimeSpan.fromTimeAndDuration(slice.ts, slice.dur);
    }
  } else {
    // TODO(primiano): unclear why we dont return undefined here.
    return new TimeSpan(Time.INVALID, Time.INVALID);
  }
}

export interface LegacySelectionDetails {
  ts?: time;
  dur?: duration;
  // Additional information for sched selection, used to draw the wakeup arrow.
  wakeupTs?: time;
  wakerCpu?: number;
}
