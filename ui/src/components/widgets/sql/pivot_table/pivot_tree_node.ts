// Copyright (C) 2025 The Android Open Source Project
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

import {range} from '../../../../base/array_utils';
import {assertExists, assertTrue} from '../../../../base/logging';
import {Row} from '../../../../trace_processor/query_result';
import {SqlValue} from '../../../../trace_processor/sql_utils';
import {LegacyTableColumn} from '../legacy_table/table_column';
import {Aggregation, basicAggregations} from './aggregations';
import {aggregationId, pivotId} from './ids';
import type {SortOrder} from './pivot_table_state';

// assertExists trips over NULLs, but NULL is a valid SQL value we have to work with.
function assertNotUndefined<T>(value: T | undefined): T {
  if (value === undefined) throw new Error('Value is undefined');
  return value;
}

interface Config {
  readonly pivots: ReadonlyArray<LegacyTableColumn>;
  readonly aggregations: ReadonlyArray<Aggregation>;
}

// A node in the pivot tree.
// Each node represents a partially aggregated values for the first `depth` pivots.
export class PivotTreeNode {
  private readonly config: Config;

  private readonly parent?: PivotTreeNode;
  // undefined only for the root node.
  // Note: it can be NULL, which can trip over assertExists.
  private readonly pivotValue?: SqlValue;
  // 0 for the root node.
  private readonly depth: number;

  private readonly children: Map<SqlValue, PivotTreeNode>;
  // The aggregated values for the node itself.
  readonly aggregationValues: SqlValue[];
  // The aggregated values for the node and all its descendants.
  private aggregations: SqlValue[];
  collapsed: boolean;

  constructor(args: {
    config: {
      pivots: ReadonlyArray<LegacyTableColumn>;
      aggregations: ReadonlyArray<Aggregation>;
    };
    parent?: PivotTreeNode;
    pivotValue?: SqlValue;
  }) {
    this.config = args.config;

    this.parent = args.parent;
    this.pivotValue = args.pivotValue;
    this.depth = this.parent === undefined ? 0 : this.parent.depth + 1;

    this.aggregationValues = range(this.config.aggregations.length).map(
      () => null,
    );
    this.aggregations = [...this.aggregationValues];
    this.children = new Map();
    this.collapsed = this.depth > 0;
  }

  // The index of the last pivot value in the pivot list.
  getPivotIndex(): number {
    return this.depth - 1;
  }

  // Return the id of the pivot which was used to create this node.
  private getPivotId(): string | undefined {
    const index = this.getPivotIndex();
    if (index === -1) return undefined;
    return pivotId(this.config.pivots[index]);
  }

  // Construct the tree from the given rows.
  // The rows should be indexed by `pivotId` and `aggregationId`.
  static buildTree(
    rows: Row[],
    config: {
      pivots: ReadonlyArray<LegacyTableColumn>;
      aggregations: ReadonlyArray<Aggregation>;
    },
  ): PivotTreeNode {
    const root = new PivotTreeNode({
      config: {
        pivots: [...config.pivots],
        aggregations: [...config.aggregations],
      },
    });
    for (const row of rows) {
      let node = root;
      for (const pivot of config.pivots) {
        node = node.getOrCreateChild(row[pivotId(pivot)]);
      }
      for (const [index, agg] of config.aggregations.entries()) {
        node.aggregationValues[index] = basicAggregations[agg.op](
          node.aggregationValues[index],
          row[aggregationId(agg)],
        );
      }
    }
    root.update();
    return root;
  }

  // Get the value of the pivot at the given index.
  getPivotValue(index: number): SqlValue | undefined {
    if (index > this.getPivotIndex()) return undefined;
    if (index === this.getPivotIndex()) return this.pivotValue;
    return assertExists(this.parent).getPivotValue(index);
  }

  /**
   * Return how the value at `pivotIndex` should be rendered for the row corresponding to this node.
   * @param pivotIndex Index of the pivot cell.
   * @returns how the value at `pivotIndex` should be rendered for the row corresponding to this node:
   * - 'expanded': 'pivotIndex' corresponds to this node's depth and the node is expanded.
   * - 'collapsed': 'pivotIndex' corresponds to this node's depth and the node is collapsed.
   * - 'last_pivot': 'pivotIndex' corresponds to this node's depth and as the last pivot can't be
   *                 neither collapsed nor expanded.
   * - 'auto_expanded': this is one of the parent pivots of this node that has been
   *   auto-expanded due to having only one child.
   * - 'pivoted_value': this is one of the parent pivots of this node and the pivoted value should be displayed.
   * - 'hidden_behind_collapsed': this is one of the child pivots of this node and the parent pivot is collapsed,
   *   so just an indication that there are some values should be displayed.
   * - 'empty': this is one of the child pivots of the expanded node and should be left empty.
   */
  getPivotDisplayStatus(
    pivotIndex: number,
  ):
    | 'expanded'
    | 'collapsed'
    | 'last_pivot'
    | 'pivoted_value'
    | 'auto_expanded'
    | 'hidden_behind_collapsed'
    | 'empty' {
    if (pivotIndex === this.getPivotIndex()) {
      if (pivotIndex + 1 === this.config.pivots.length) {
        return 'last_pivot';
      }
      return this.collapsed ? 'collapsed' : 'expanded';
    }
    if (pivotIndex > this.getPivotIndex()) {
      return this.collapsed ? 'hidden_behind_collapsed' : 'empty';
    }
    // Find the node responsible for the value at `pivotIndex`.
    let valueNode: PivotTreeNode = this;
    let autoExpanded = true;
    for (let i = pivotIndex; i < this.getPivotIndex(); i++) {
      valueNode = assertExists(valueNode.parent);
      autoExpanded = autoExpanded && valueNode.children.size === 1;
    }
    return autoExpanded ? 'auto_expanded' : 'pivoted_value';
  }

  // Get the value of the aggregation at the given index.
  getAggregationValue(index: number): SqlValue {
    return this.aggregations[index];
  }

  // List all of the descendants of this node, respecting `collapsed` state.
  *listDescendants(): Generator<PivotTreeNode> {
    if (this.children.size !== 1) {
      // Skip the nodes with only one child.
      yield this;
      // Skip collapsed nodes, but ignore this for nodes with only one child, which should be auto-expanded.
      if (this.collapsed) return;
    }
    for (const child of this.children.values()) {
      yield* child.listDescendants();
    }
  }

  // Recursively sort the subtree according to the given order.
  sort(order: SortOrder) {
    if (order.length === 0) return;

    for (const child of this.children.values()) {
      child.sort(order);
    }
    const sorted = [...this.children.values()].sort((lhs, rhs) =>
      PivotTreeNode.compare(lhs, rhs, order),
    );
    this.children.clear();
    for (const child of sorted) {
      this.children.set(assertNotUndefined(child.pivotValue), child);
    }
  }

  // Recursively copy the expanded state from the old pivot tree, trying to preserve
  // the user expanded nodes as much as possible.
  // We copy the status from the nodes which have the same pivot prefix (values
  // and pivots themselves).
  copyExpandedState(oldNode?: PivotTreeNode) {
    if (oldNode === undefined) return;
    // We should only try to copy the state of nodes with the same pivot index.
    assertTrue(this.getPivotIndex() === oldNode.getPivotIndex());
    if (this.getPivotId() !== oldNode.getPivotId()) return;

    this.collapsed = oldNode.collapsed;
    for (const [value, child] of this.children) {
      child.copyExpandedState(oldNode.children.get(value));
    }
  }

  private getOrCreateChild(value: SqlValue): PivotTreeNode {
    if (!this.children.has(value)) {
      this.children.set(
        value,
        new PivotTreeNode({
          config: this.config,
          parent: this,
          pivotValue: value,
        }),
      );
    }
    return assertExists(this.children.get(value));
  }

  private update() {
    this.aggregations = [...this.aggregationValues];
    for (const child of this.children.values()) {
      child.update();
      for (const [index, agg] of this.config.aggregations.entries()) {
        this.aggregations[index] = basicAggregations[agg.op](
          this.aggregations[index],
          child.aggregations[index],
        );
      }
    }
  }

  // Compare two nodes according to the given sort order.
  private static compare(
    lhs: PivotTreeNode,
    rhs: PivotTreeNode,
    order: SortOrder,
  ): number {
    // Note: resolving items in `order` requires a lookup in the state. We can consider
    // optimising this and performing the lookup in `sort` instead.

    // We should only compare siblings.
    assertTrue(
      lhs.config === rhs.config &&
        lhs.depth === rhs.depth &&
        lhs.parent === rhs.parent,
    );

    const compareSqlValues = (lhs: SqlValue, rhs: SqlValue) => {
      if (lhs === rhs) return 0;
      // Nulls can't be compared, but should be considered the smallest value.
      if (lhs === null) return -1;
      if (rhs === null) return 1;
      return lhs < rhs ? -1 : 1;
    };
    for (const {type, id, direction} of order) {
      if (type === 'aggregation') {
        const index = lhs.config.aggregations.findIndex(
          (a) => aggregationId(a) === id,
        );
        // Aggregation with this index should always exist.
        // If this is not the case, we probably failed to remove sorting after
        // hiding a column.
        assertTrue(index !== -1);
        const cmp = compareSqlValues(
          lhs.aggregations[index],
          rhs.aggregations[index],
        );
        if (cmp !== 0) return direction === 'ASC' ? cmp : -cmp;
      } else {
        const index = lhs.config.pivots.findIndex((p) => pivotId(p) === id);
        // Pivot with this index should always exist.
        // If this is not the case, we probably failed to remove sorting after
        // hiding a column.
        assertTrue(index !== -1);
        // For pivot sorting, we only compare the pivot values at the given depth.
        if (index + 1 === lhs.depth) {
          const cmp = compareSqlValues(
            assertNotUndefined(lhs.pivotValue),
            assertNotUndefined(rhs.pivotValue),
          );
          if (cmp !== 0) return direction === 'ASC' ? cmp : -cmp;
        }
      }
    }
    return 0;
  }
}
