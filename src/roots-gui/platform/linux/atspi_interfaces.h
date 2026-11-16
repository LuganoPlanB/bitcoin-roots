// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
// AT-SPI2 D-Bus API signatures: GNOME at-spi2-core 2.56.2 xml/, see docs/linux-accessibility.md.
#pragma once
namespace roots::gui::platform {
inline constexpr char AtspiInterfaces[]=R"AXXML(<node>
  <interface name="org.a11y.atspi.Accessible">
    <property name="Name" type="s" access="read" />
    <property name="Description" type="s" access="read" />
    <property name="Parent" type="(so)" access="read" />
    <property name="ChildCount" type="i" access="read" />
    <property name="Locale" type="s" access="read" />
    <property name="AccessibleId" type="s" access="read" />
    <property name="HelpText" type="s" access="read" />
    <method name="GetChildAtIndex">
      <arg direction="in" name="index" type="i" />
      <arg direction="out" type="(so)" />
    </method>
    <method name="GetChildren">
      <arg direction="out" type="a(so)" />
    </method>
    <method name="GetIndexInParent">
      <arg direction="out" type="i" />
    </method>
    <method name="GetRelationSet">
      <arg direction="out" type="a(ua(so))" />
    </method>
    <method name="GetRole">
      <arg direction="out" type="u" />
    </method>
    <method name="GetRoleName">
      <arg direction="out" type="s" />
    </method>
    <method name="GetLocalizedRoleName">
      <arg direction="out" type="s" />
    </method>
    <method name="GetState">
      <arg direction="out" type="au" />
    </method>
    <method name="GetAttributes">
      <arg direction="out" type="a{ss}" />
    </method>
    <method name="GetApplication">
      <arg direction="out" type="(so)" />
    </method>
    <method name="GetInterfaces">
      <arg direction="out" type="as" />
    </method>
  </interface>
  <interface name="org.a11y.atspi.Application">
    <property name="ToolkitName" type="s" access="read" />
    <property name="Version" type="s" access="read" />
    <property name="AtspiVersion" type="s" access="read" />
    <property name="Id" type="i" access="readwrite" />
    <method name="GetLocale">
      <arg direction="in" name="lctype" type="u" />
      <arg direction="out" type="s" />
    </method>
  </interface>
  <interface name="org.a11y.atspi.Action">
    <property name="NActions" type="i" access="read" />
    <method name="GetDescription">
      <arg type="i" name="index" direction="in" />
      <arg type="s" direction="out" />
    </method>
    <method name="GetName">
      <arg type="i" name="index" direction="in" />
      <arg type="s" direction="out" />
    </method>
    <method name="GetLocalizedName">
      <arg type="i" name="index" direction="in" />
      <arg type="s" direction="out" />
    </method>
    <method name="GetKeyBinding">
      <arg type="i" name="index" direction="in" />
      <arg type="s" direction="out" />
    </method>
    <method name="GetActions">
      <arg direction="out" type="a(sss)" />
    </method>
    <method name="DoAction">
      <arg direction="in" name="index" type="i" />
      <arg direction="out" type="b" />
    </method>
  </interface>
  <interface name="org.a11y.atspi.Component">
    <method name="Contains">
      <arg direction="in" name="x" type="i" />
      <arg direction="in" name="y" type="i" />
      <arg direction="in" name="coord_type" type="u" />
      <arg direction="out" type="b" />
    </method>
    <method name="GetAccessibleAtPoint">
      <arg direction="in" name="x" type="i" />
      <arg direction="in" name="y" type="i" />
      <arg direction="in" name="coord_type" type="u" />
      <arg direction="out" type="(so)" />
    </method>
    <method name="GetExtents">
      <arg direction="in" name="coord_type" type="u" />
      <arg direction="out" type="(iiii)" />
    </method>
    <method name="GetPosition">
      <arg direction="in" name="coord_type" type="u" />
      <arg direction="out" name="x" type="i" />
      <arg direction="out" name="y" type="i" />
    </method>
    <method name="GetSize">
      <arg direction="out" name="width" type="i" />
      <arg direction="out" name="height" type="i" />
    </method>
    <method name="GetLayer">
      <arg direction="out" type="u" />
    </method>
    <method name="GetMDIZOrder">
      <arg direction="out" type="n" />
    </method>
    <method name="GrabFocus">
      <arg direction="out" type="b" />
    </method>
    <method name="GetAlpha">
      <arg direction="out" type="d" />
    </method>
    <method name="SetExtents">
      <arg direction="in" name="x" type="i" />
      <arg direction="in" name="y" type="i" />
      <arg direction="in" name="width" type="i" />
      <arg direction="in" name="height" type="i" />
      <arg direction="in" name="coord_type" type="u" />
      <arg direction="out" type="b" />
    </method>
    <method name="SetPosition">
      <arg direction="in" name="x" type="i" />
      <arg direction="in" name="y" type="i" />
      <arg direction="in" name="coord_type" type="u" />
      <arg direction="out" type="b" />
    </method>
    <method name="SetSize">
      <arg direction="in" name="width" type="i" />
      <arg direction="in" name="height" type="i" />
      <arg direction="out" type="b" />
    </method>
    <method name="ScrollTo">
      <arg direction="in" name="type" type="u" />
      <arg direction="out" type="b" />
    </method>
    <method name="ScrollToPoint">
      <arg direction="in" name="coord_type" type="u" />
      <arg direction="in" name="x" type="i" />
      <arg direction="in" name="y" type="i" />
      <arg direction="out" type="b" />
    </method>
  </interface>
  <interface name="org.a11y.atspi.Text">
    <property name="CharacterCount" type="i" access="read" />
    <property name="CaretOffset" type="i" access="read" />
    <method name="GetStringAtOffset">
      <arg direction="in" name="offset" type="i" />
      <arg direction="in" name="granularity" type="u" />
      <arg direction="out" type="s" />
      <arg direction="out" name="startOffset" type="i" />
      <arg direction="out" name="endOffset" type="i" />
    </method>
    <method name="GetText">
      <arg direction="in" name="startOffset" type="i" />
      <arg direction="in" name="endOffset" type="i" />
      <arg direction="out" type="s" />
    </method>
    <method name="SetCaretOffset">
      <arg direction="in" name="offset" type="i" />
      <arg direction="out" type="b" />
    </method>
    <method name="GetTextBeforeOffset">
      <arg direction="in" name="offset" type="i" />
      <arg direction="in" name="type" type="u" />
      <arg direction="out" type="s" />
      <arg direction="out" name="startOffset" type="i" />
      <arg direction="out" name="endOffset" type="i" />
    </method>
    <method name="GetTextAtOffset">
      <arg direction="in" name="offset" type="i" />
      <arg direction="in" name="type" type="u" />
      <arg direction="out" type="s" />
      <arg direction="out" name="startOffset" type="i" />
      <arg direction="out" name="endOffset" type="i" />
    </method>
    <method name="GetTextAfterOffset">
      <arg direction="in" name="offset" type="i" />
      <arg direction="in" name="type" type="u" />
      <arg direction="out" type="s" />
      <arg direction="out" name="startOffset" type="i" />
      <arg direction="out" name="endOffset" type="i" />
    </method>
    <method name="GetCharacterAtOffset">
      <arg direction="in" name="offset" type="i" />
      <arg direction="out" type="i" />
    </method>
    <method name="GetAttributeValue">
      <arg direction="in" name="offset" type="i" />
      <arg direction="in" name="attributeName" type="s" />
      <arg direction="out" type="s" />
    </method>
    <method name="GetAttributes">
      <arg direction="in" name="offset" type="i" />
      <arg direction="out" type="a{ss}" />
      <arg direction="out" name="startOffset" type="i" />
      <arg direction="out" name="endOffset" type="i" />
    </method>
    <method name="GetDefaultAttributes">
      <arg direction="out" type="a{ss}" />
    </method>
    <method name="GetCharacterExtents">
      <arg direction="in" name="offset" type="i" />
      <arg direction="in" name="coordType" type="u" />
      <arg direction="out" name="x" type="i" />
      <arg direction="out" name="y" type="i" />
      <arg direction="out" name="width" type="i" />
      <arg direction="out" name="height" type="i" />
    </method>
    <method name="GetOffsetAtPoint">
      <arg direction="in" name="x" type="i" />
      <arg direction="in" name="y" type="i" />
      <arg direction="in" name="coordType" type="u" />
      <arg direction="out" type="i" />
    </method>
    <method name="GetNSelections">
      <arg direction="out" type="i" />
    </method>
    <method name="GetSelection">
      <arg direction="in" name="selectionNum" type="i" />
      <arg direction="out" name="startOffset" type="i" />
      <arg direction="out" name="endOffset" type="i" />
    </method>
    <method name="AddSelection">
      <arg direction="in" name="startOffset" type="i" />
      <arg direction="in" name="endOffset" type="i" />
      <arg direction="out" type="b" />
    </method>
    <method name="RemoveSelection">
      <arg direction="in" name="selectionNum" type="i" />
      <arg direction="out" type="b" />
    </method>
    <method name="SetSelection">
      <arg direction="in" name="selectionNum" type="i" />
      <arg direction="in" name="startOffset" type="i" />
      <arg direction="in" name="endOffset" type="i" />
      <arg direction="out" type="b" />
    </method>
    <method name="GetRangeExtents">
      <arg direction="in" name="startOffset" type="i" />
      <arg direction="in" name="endOffset" type="i" />
      <arg direction="in" name="coordType" type="u" />
      <arg direction="out" name="x" type="i" />
      <arg direction="out" name="y" type="i" />
      <arg direction="out" name="width" type="i" />
      <arg direction="out" name="height" type="i" />
    </method>
    <method name="GetBoundedRanges">
      <arg direction="in" name="x" type="i" />
      <arg direction="in" name="y" type="i" />
      <arg direction="in" name="width" type="i" />
      <arg direction="in" name="height" type="i" />
      <arg direction="in" name="coordType" type="u" />
      <arg direction="in" name="xClipType" type="u" />
      <arg direction="in" name="yClipType" type="u" />
      <arg direction="out" type="a(iisv)" />
    </method>
    <method name="GetAttributeRun">
      <arg direction="in" name="offset" type="i" />
      <arg direction="in" name="includeDefaults" type="b" />
      <arg direction="out" type="a{ss}" />
      <arg direction="out" name="startOffset" type="i" />
      <arg direction="out" name="endOffset" type="i" />
    </method>
    <method name="GetDefaultAttributeSet">
      <arg direction="out" type="a{ss}" />
    </method>
    <method name="ScrollSubstringTo">
      <arg direction="in" name="startOffset" type="i" />
      <arg direction="in" name="endOffset" type="i" />
      <arg direction="in" name="type" type="u" />
      <arg direction="out" type="b" />
    </method>
    <method name="ScrollSubstringToPoint">
      <arg direction="in" name="startOffset" type="i" />
      <arg direction="in" name="endOffset" type="i" />
      <arg direction="in" name="type" type="u" />
      <arg direction="in" name="x" type="i" />
      <arg direction="in" name="y" type="i" />
      <arg direction="out" type="b" />
    </method>
  </interface>
  <interface name="org.a11y.atspi.EditableText">
    <method name="SetTextContents">
      <arg direction="in" name="newContents" type="s" />
      <arg direction="out" type="b" />
    </method>
    <method name="InsertText">
      <arg direction="in" name="position" type="i" />
      <arg direction="in" name="text" type="s" />
      <arg direction="in" name="length" type="i" />
      <arg direction="out" type="b" />
    </method>
    <method name="CopyText">
      <arg direction="in" name="startPos" type="i" />
      <arg direction="in" name="endPos" type="i" />
    </method>
    <method name="CutText">
      <arg direction="in" name="startPos" type="i" />
      <arg direction="in" name="endPos" type="i" />
      <arg direction="out" type="b" />
    </method>
    <method name="DeleteText">
      <arg direction="in" name="startPos" type="i" />
      <arg direction="in" name="endPos" type="i" />
      <arg direction="out" type="b" />
    </method>
    <method name="PasteText">
      <arg direction="in" name="position" type="i" />
      <arg direction="out" type="b" />
    </method>
  </interface>
  <interface name="org.a11y.atspi.Selection">
    <property name="NSelectedChildren" type="i" access="read" />
    <method name="GetSelectedChild">
      <arg direction="in" name="selectedChildIndex" type="i" />
      <arg direction="out" type="(so)" />
    </method>
    <method name="SelectChild">
      <arg direction="in" name="childIndex" type="i" />
      <arg direction="out" type="b" />
    </method>
    <method name="DeselectSelectedChild">
      <arg direction="in" name="selectedChildIndex" type="i" />
      <arg direction="out" type="b" />
    </method>
    <method name="IsChildSelected">
      <arg direction="in" name="childIndex" type="i" />
      <arg direction="out" type="b" />
    </method>
    <method name="SelectAll">
      <arg direction="out" type="b" />
    </method>
    <method name="ClearSelection">
      <arg direction="out" type="b" />
    </method>
    <method name="DeselectChild">
      <arg direction="in" name="childIndex" type="i" />
      <arg direction="out" type="b" />
    </method>
  </interface>
  <interface name="org.a11y.atspi.Table">
    <property name="NRows" type="i" access="read" />
    <property name="NColumns" type="i" access="read" />
    <property name="Caption" type="(so)" access="read" />
    <property name="Summary" type="(so)" access="read" />
    <property name="NSelectedRows" type="i" access="read" />
    <property name="NSelectedColumns" type="i" access="read" />
    <method name="GetAccessibleAt">
      <arg direction="in" name="row" type="i" />
      <arg direction="in" name="column" type="i" />
      <arg direction="out" type="(so)" />
    </method>
    <method name="GetIndexAt">
      <arg direction="in" name="row" type="i" />
      <arg direction="in" name="column" type="i" />
      <arg direction="out" type="i" />
    </method>
    <method name="GetRowAtIndex">
      <arg direction="in" name="index" type="i" />
      <arg direction="out" type="i" />
    </method>
    <method name="GetColumnAtIndex">
      <arg direction="in" name="index" type="i" />
      <arg direction="out" type="i" />
    </method>
    <method name="GetRowDescription">
      <arg direction="in" name="row" type="i" />
      <arg direction="out" type="s" />
    </method>
    <method name="GetColumnDescription">
      <arg direction="in" name="column" type="i" />
      <arg direction="out" type="s" />
    </method>
    <method name="GetRowExtentAt">
      <arg direction="in" name="row" type="i" />
      <arg direction="in" name="column" type="i" />
      <arg direction="out" type="i" />
    </method>
    <method name="GetColumnExtentAt">
      <arg direction="in" name="row" type="i" />
      <arg direction="in" name="column" type="i" />
      <arg direction="out" type="i" />
    </method>
    <method name="GetRowHeader">
      <arg direction="in" name="row" type="i" />
      <arg direction="out" type="(so)" />
    </method>
    <method name="GetColumnHeader">
      <arg direction="in" name="column" type="i" />
      <arg direction="out" type="(so)" />
    </method>
    <method name="GetSelectedRows">
      <arg direction="out" type="ai" />
    </method>
    <method name="GetSelectedColumns">
      <arg direction="out" type="ai" />
    </method>
    <method name="IsRowSelected">
      <arg direction="in" name="row" type="i" />
      <arg direction="out" type="b" />
    </method>
    <method name="IsColumnSelected">
      <arg direction="in" name="column" type="i" />
      <arg direction="out" type="b" />
    </method>
    <method name="IsSelected">
      <arg direction="in" name="row" type="i" />
      <arg direction="in" name="column" type="i" />
      <arg direction="out" type="b" />
    </method>
    <method name="AddRowSelection">
      <arg direction="in" name="row" type="i" />
      <arg direction="out" type="b" />
    </method>
    <method name="AddColumnSelection">
      <arg direction="in" name="column" type="i" />
      <arg direction="out" type="b" />
    </method>
    <method name="RemoveRowSelection">
      <arg direction="in" name="row" type="i" />
      <arg direction="out" type="b" />
    </method>
    <method name="RemoveColumnSelection">
      <arg direction="in" name="column" type="i" />
      <arg direction="out" type="b" />
    </method>
    <method name="GetRowColumnExtentsAtIndex">
      <arg direction="in" name="index" type="i" />
      <arg direction="out" type="b" />
      <arg direction="out" name="row" type="i" />
      <arg direction="out" name="col" type="i" />
      <arg direction="out" name="row_extents" type="i" />
      <arg direction="out" name="col_extents" type="i" />
      <arg direction="out" name="is_selected" type="b" />
    </method>
  </interface>
  <interface name="org.a11y.atspi.TableCell">
    <property access="read" name="ColumnSpan" type="i" />
    <property access="read" name="Position" type="(ii)" />
    <property access="read" name="RowSpan" type="i" />
    <property access="read" name="Table" type="(so)" />
    <method name="GetRowColumnSpan">
      <arg direction="out" type="b" />
      <arg direction="out" name="row" type="i" />
      <arg direction="out" name="col" type="i" />
      <arg direction="out" name="row_extents" type="i" />
      <arg direction="out" name="col_extents" type="i" />
    </method>
    <method name="GetColumnHeaderCells">
      <arg direction="out" type="a(so)" />
    </method>
    <method name="GetRowHeaderCells">
      <arg direction="out" type="a(so)" />
    </method>
  </interface>
  <interface name="org.a11y.atspi.Value">
    <property name="MinimumValue" type="d" access="read" />
    <property name="MaximumValue" type="d" access="read" />
    <property name="MinimumIncrement" type="d" access="read" />
    <property name="CurrentValue" type="d" access="readwrite" />
    <property name="Text" type="s" access="read" />
  </interface>
  <interface name="org.a11y.atspi.Event.Object">
    <signal name="PropertyChange">
      <arg name="property" type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg name="value" type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="BoundsChanged">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="LinkSelected">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="StateChanged">
      <arg name="state" type="s" />
      <arg name="enabled" type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="ChildrenChanged">
      <arg name="operation" type="s" />
      <arg name="index_in_parent" type="i" />
      <arg type="i" />
      <arg name="child" type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="VisibleDataChanged">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="SelectionChanged">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="ModelChanged">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="ActiveDescendantChanged">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg name="child" type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="Announcement">
      <arg type="s" />
      <arg type="i" name="politeness" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="AttributesChanged">
      <arg name="name" type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="RowInserted">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="RowReordered">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="RowDeleted">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="ColumnInserted">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="ColumnReordered">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="ColumnDeleted">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="TextBoundsChanged">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="TextSelectionChanged">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="TextChanged">
      <arg name="detail" type="s" />
      <arg name="start_pos" type="i" />
      <arg name="length" type="i" />
      <arg name="text" type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="TextAttributesChanged">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="TextCaretMoved">
      <arg type="s" />
      <arg name="position" type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
  </interface>
  <interface name="org.a11y.atspi.Event.Window">
    <signal name="PropertyChange">
      <arg name="property" type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="Minimize">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="Maximize">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="Restore">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="Close">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="Create">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="Reparent">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="DesktopCreate">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="DesktopDestroy">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="Destroy">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="Activate">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="Deactivate">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="Raise">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="Lower">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="Move">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="Resize">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="Shade">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="uUshade">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
    <signal name="Restyle">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
  </interface>
  <interface name="org.a11y.atspi.Event.Focus">
    <signal name="Focus">
      <arg type="s" />
      <arg type="i" />
      <arg type="i" />
      <arg type="v" />
      <arg name="properties" type="a{sv}" />
    </signal>
  </interface>
  <interface name="org.a11y.atspi.Cache">
    <method name="GetItems">
      <arg direction="out" name="nodes" type="a((so)(so)(so)iiassusau)" />
    </method>
    <signal name="AddAccessible">
      <arg name="nodeAdded" type="((so)(so)(so)iiassusau)" />
    </signal>
    <signal name="RemoveAccessible">
      <arg name="nodeRemoved" type="(so)" />
    </signal>
  </interface>
</node>)AXXML";
}
