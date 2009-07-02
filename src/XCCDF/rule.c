/*
 * Copyright 2009 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors:
 *      Lukas Kuklinek <lkuklinek@redhat.com>
 */


#include "item.h"
#include "elements.h"
#include <assert.h>
#include <string.h>

bool xccdf_content_parse(xmlTextReaderPtr reader, struct xccdf_item* parent)
{
	assert(parent != NULL);

	struct xccdf_list* list;
	struct xccdf_item* item = NULL;

	switch (parent->type) {
		case XCCDF_BENCHMARK: list = parent->sub.bench.content; break;
		case XCCDF_GROUP: list = parent->sub.group.content; break;
		default: assert(false);
	}

	assert(list != NULL);

	switch (xccdf_element_get(reader)) {
		case XCCDFE_RULE:  item = xccdf_rule_new_parse(reader, parent);  break;
		case XCCDFE_GROUP: item = xccdf_group_new_parse(reader, parent); break;
		case XCCDFE_VALUE: /** @todo implement */ break;
		default: assert(false);
	}

	if (item != NULL) {
		xccdf_list_add(list, item);
		return true;
	}

	return false;
}

char* xccdf_strsep(char** str, char delim)
{
	if (str == NULL || *str == NULL) return NULL;
	char* ret = *str;
	*str = strchr(*str, delim);
	if (*str) {
		**str = '\0';
		(*str)++;
	}
	return ret;
}

void xccdf_deps_get(struct xccdf_item* item, struct xccdf_list** conflicts, struct xccdf_list** requires)
{
	switch (item->type) {
		case XCCDF_RULE:
			if (conflicts) *conflicts = item->sub.rule.conflicts;
			if (requires)  *requires  = item->sub.rule.requires;
			break;
		case XCCDF_GROUP:
			if (conflicts) *conflicts = item->sub.group.conflicts;
			if (requires)  *requires  = item->sub.group.requires;
			break;
		default: assert(false);
	}
}

bool xccdf_parse_deps(xmlTextReaderPtr reader, struct xccdf_item* item)
{
	struct xccdf_list *conflicts, *requires;
	xccdf_deps_get(item, &conflicts, &requires);

	switch (xccdf_element_get(reader)) {
		case XCCDFE_REQUIRES: {
			struct xccdf_list* reqs = xccdf_list_new();
			char *ids = xccdf_attribute_copy(reader, XCCDFA_IDREF), *idsstr = ids, *id;

			while ((id = xccdf_strsep(&ids, ' ')) != NULL) {
				if (strcmp(id, "") == 0) continue;
				xccdf_list_add(reqs, NULL);
				xccdf_benchmark_add_ref(item->item.benchmark, (struct xccdf_item**)&reqs->last->data, id, XCCDF_CONTENT);
			}
			if (reqs->itemcount == 0) {
				xccdf_list_delete(reqs, NULL);
				return false;
			}

			xccdf_list_add(requires, reqs);
			free(idsstr);
			break;
		}
		case XCCDFE_CONFLICTS:
			xccdf_list_add(conflicts, NULL);
			xccdf_benchmark_add_ref(item->item.benchmark, (struct xccdf_item**)&conflicts->last->data,
					xccdf_attribute_get(reader, XCCDFA_IDREF), XCCDF_CONTENT);
			break;
		default: assert(false);
	}

	return true;
}

void xccdf_print_id_list(struct xccdf_list* items, const char* sep)
{
	struct xccdf_list_item* it;
	if (sep == NULL) sep = ", ";
	for (it = items->first; it; it = it->next) {
		if (it != items->first) printf("%s", sep);
		printf("%s", XITEM(it->data)->item.id);
	}
}

void xccdf_deps_dump(struct xccdf_item* item, int depth)
{
	struct xccdf_list *conflicts, *requires;
	xccdf_deps_get(item, &conflicts, &requires);

	if (requires->itemcount > 0) {
		struct xccdf_list_item* it;
		xccdf_print_depth(depth); printf("requires: ");
		for (it = requires->first; it; it = it->next) {
			struct xccdf_list *nlist = it->data;
			if (it != requires->first) printf(" & ");
			if (nlist->itemcount <= 0) continue;
			if (nlist->itemcount > 1) printf("(");
			xccdf_print_id_list(nlist, " | ");
			if (nlist->itemcount > 1) printf(")");
		}
		printf("\n");
	}

	if (conflicts->itemcount > 0) {
		xccdf_print_depth(depth); printf("conflicts: ");
		xccdf_print_id_list(conflicts, " | ");
		printf("\n");
	}
}

struct xccdf_item* xccdf_group_new_empty(struct xccdf_item* parent)
{
	struct xccdf_item* group = xccdf_item_new(XCCDF_GROUP, parent->item.benchmark, parent);
	group->sub.group.content = xccdf_list_new();
	group->sub.group.requires = xccdf_list_new();
	group->sub.group.conflicts = xccdf_list_new();
	group->sub.group.values = xccdf_list_new();
	return group;
}

struct xccdf_item* xccdf_group_new_parse(xmlTextReaderPtr reader, struct xccdf_item* parent)
{
	XCCDF_ASSERT_ELEMENT(reader, XCCDFE_GROUP);

	struct xccdf_item* group = xccdf_group_new_empty(parent);
	
	if (!xccdf_item_process_attributes(group, reader)) {
		xccdf_group_delete(group);
		return NULL;
	}

	int depth = xccdf_element_depth(reader) + 1;

	while (xccdf_to_start_element(reader, depth)) {
		switch (xccdf_element_get(reader)) {
			case XCCDFE_REQUIRES: case XCCDFE_CONFLICTS:
				xccdf_parse_deps(reader, group);
				break;
			case XCCDFE_GROUP: case XCCDFE_RULE:
				xccdf_content_parse(reader, group);
				break;
			case XCCDFE_VALUE:
				xccdf_list_add(group->sub.group.values, xccdf_value_new_parse(reader, group));
				break;
			default: xccdf_item_process_element(group, reader);
		}
		xmlTextReaderRead(reader);
	}

	return group;
}

void xccdf_group_dump(struct xccdf_item* group, int depth)
{
	xccdf_print_depth(depth);
	printf("Group : %s\n", (group ? group->item.id : "(NULL)"));
	if (group) {
        xccdf_item_print(group, depth + 1);
		xccdf_print_depth(depth + 1);
		printf("values"); xccdf_list_dump(group->sub.group.values, (xccdf_dump_func)xccdf_value_dump, depth + 2);
		xccdf_print_depth(depth + 1);
		printf("content"); xccdf_list_dump(group->sub.group.content, (xccdf_dump_func)xccdf_item_dump, depth + 2);
	}
}

void xccdf_group_delete(struct xccdf_item* group)
{
	if (group) {
		xccdf_list_delete(group->sub.group.content, (xccdf_destruct_func)xccdf_item_delete);
		xccdf_list_delete(group->sub.group.requires, (xccdf_destruct_func)xccdf_list_delete0);
		xccdf_list_delete(group->sub.group.values, (xccdf_destruct_func)xccdf_value_delete);
		xccdf_list_delete(group->sub.group.conflicts, NULL);
		xccdf_item_release(group);
	}
}

struct xccdf_item* xccdf_rule_new_empty(struct xccdf_item* parent)
{
	struct xccdf_item* rule = xccdf_item_new(XCCDF_RULE, parent->item.benchmark, parent);
	rule->sub.rule.role = XCCDF_ROLE_FULL;
	rule->sub.rule.severity = XCCDF_UNKNOWN;
	rule->sub.rule.idents = xccdf_list_new();
	rule->sub.rule.checks = xccdf_list_new();
	rule->sub.rule.requires = xccdf_list_new();
	rule->sub.rule.conflicts = xccdf_list_new();
	rule->sub.rule.profile_notes = xccdf_list_new();
	rule->sub.rule.fixes = xccdf_list_new();
	rule->sub.rule.fixtexts = xccdf_list_new();
	return rule;
}


struct xccdf_item* xccdf_rule_new_parse(xmlTextReaderPtr reader, struct xccdf_item* parent)
{
	XCCDF_ASSERT_ELEMENT(reader, XCCDFE_RULE);

	struct xccdf_item* rule = xccdf_rule_new_empty(parent);
	
	if (!xccdf_item_process_attributes(rule, reader)) {
		xccdf_rule_delete(rule);
		return NULL;
	}
	if (xccdf_attribute_has(reader, XCCDFA_ROLE))
		rule->sub.rule.role = string_to_enum(XCCDF_ROLE_MAP, xccdf_attribute_get(reader, XCCDFA_ROLE));
	if (xccdf_attribute_has(reader, XCCDFA_SEVERITY))
		rule->sub.rule.severity = string_to_enum(XCCDF_LEVEL_MAP, xccdf_attribute_get(reader, XCCDFA_SEVERITY));

	int depth = xccdf_element_depth(reader) + 1;

	while (xccdf_to_start_element(reader, depth)) {
		switch (xccdf_element_get(reader)) {
			case XCCDFE_REQUIRES: case XCCDFE_CONFLICTS:
				xccdf_parse_deps(reader, rule);
				break;
			case XCCDFE_PROFILE_NOTE: {
				const char* tag = xccdf_attribute_get(reader, XCCDFA_TAG);
				if (tag == NULL) break;
				struct xccdf_profile_note* note = calloc(1, sizeof(struct xccdf_profile_note));
				note->reftag = strdup(tag);
				note->text = xccdf_get_xml(reader);
				xccdf_list_add(rule->sub.rule.profile_notes, note);
				break;
			}
			case XCCDFE_CHECK: {
				struct xccdf_check* check = xccdf_check_new_parse(reader, rule);
				if (check == NULL) break;
				if (check->selector == NULL || strcmp(check->selector, "") == 0) rule->sub.rule.check = check;
				xccdf_list_add(rule->sub.rule.checks, check);
				break;
			}
			case XCCDFE_FIX:
				xccdf_list_add(rule->sub.rule.fixes, xccdf_fix_new_parse(reader, rule));
				break;
			case XCCDFE_FIXTEXT:
				xccdf_list_add(rule->sub.rule.fixtexts, xccdf_fixtext_new_parse(reader, rule));
				break;
			case XCCDFE_IDENT:
				xccdf_list_add(rule->sub.rule.idents, xccdf_ident_new_parse(reader));
				break;
			default: xccdf_item_process_element(rule, reader);
		}
		xmlTextReaderRead(reader);
	}

	return rule;
}

void xccdf_rule_dump(struct xccdf_item* rule, int depth)
{
	xccdf_print_depth(depth);
	printf("Rule : %s\n", (rule ? rule->item.id : "(NULL)"));
	if (rule) {
        xccdf_item_print(rule, depth + 1);
		xccdf_deps_dump(rule, depth + 1);
		xccdf_print_depth(depth+1); printf("idents"); xccdf_list_dump(rule->sub.rule.idents, (xccdf_dump_func)xccdf_ident_dump, depth+2);
		xccdf_print_depth(depth+1); printf("checks"); xccdf_list_dump(rule->sub.rule.checks, (xccdf_dump_func)xccdf_check_dump, depth+2);
	}
}

void xccdf_rule_delete(struct xccdf_item* rule)
{
	if (rule) {
		xccdf_list_delete(rule->sub.rule.idents, (xccdf_destruct_func)xccdf_ident_delete);
		xccdf_list_delete(rule->sub.rule.checks, (xccdf_destruct_func)xccdf_check_delete);
		xccdf_list_delete(rule->sub.rule.requires, (xccdf_destruct_func)xccdf_list_delete0);
		xccdf_list_delete(rule->sub.rule.conflicts, NULL);
		xccdf_list_delete(rule->sub.rule.profile_notes, (xccdf_destruct_func)xccdf_profile_note_delete);
		xccdf_list_delete(rule->sub.rule.fixes, (xccdf_destruct_func)xccdf_fix_delete);
		xccdf_list_delete(rule->sub.rule.fixtexts, (xccdf_destruct_func)xccdf_fixtext_delete);
		xccdf_item_release(rule);
	}
}

struct xccdf_ident* xccdf_ident_new(const char* id, const char* sys)
{
	if (id == NULL || sys == NULL) return NULL;
	struct xccdf_ident* ident = calloc(1, sizeof(struct xccdf_ident));
	ident->id = strdup(id);
	ident->system = strdup(sys);
	return ident;
}

struct xccdf_ident* xccdf_ident_new_parse(xmlTextReaderPtr reader)
{
	XCCDF_ASSERT_ELEMENT(reader, XCCDFE_IDENT);
	const char* sys = xccdf_attribute_get(reader, XCCDFA_SYSTEM);
	const char* id  = xccdf_element_string_get(reader);
	return xccdf_ident_new(id, sys);
}

void xccdf_ident_dump(struct xccdf_ident* ident, int depth)
{
	xccdf_print_depth(depth); printf("ident : %s => %s\n", ident->system, ident->id);
}

void xccdf_ident_delete(struct xccdf_ident* ident)
{
	if (ident) {
		free(ident->id);
		free(ident->system);
		free(ident);
	}
}

void xccdf_profile_note_delete(struct xccdf_profile_note* note)
{
	if (note) {
		free(note->reftag);
		free(note->text);
		free(note);
	}
}

XCCDF_GENERIC_GETTER(const char*, ident, id)
XCCDF_GENERIC_GETTER(const char*, ident, system)

struct xccdf_check* xccdf_check_new_empty(struct xccdf_item* parent)
{
	struct xccdf_check* check = calloc(1, sizeof(struct xccdf_check));
	check->parent = parent;
	check->content_refs = xccdf_list_new();
	check->imports = xccdf_list_new();
	check->exports = xccdf_list_new();
	check->children = xccdf_list_new();
	return check;
}

const struct xccdf_string_map XCCDF_BOOLOP_MAP[] = {
	{ XCCDF_OPERATOR_AND, "and" }, { XCCDF_OPERATOR_AND, "AND" },
	{ XCCDF_OPERATOR_OR,  "or"  }, { XCCDF_OPERATOR_OR,  "OR"  },
	{ 0, NULL }
};

struct xccdf_check* xccdf_check_new_parse(xmlTextReaderPtr reader, struct xccdf_item* parent)
{
	enum xccdf_element el = xccdf_element_get(reader);
	if (el != XCCDFE_CHECK && el != XCCDFE_COMPLEX_CHECK) return NULL;
	struct xccdf_check* check = xccdf_check_new_empty(parent);

	check->id = xccdf_attribute_copy(reader, XCCDFA_ID);
	check->system = xccdf_attribute_copy(reader, XCCDFA_SYSTEM);
	check->selector = xccdf_attribute_copy(reader, XCCDFA_SELECTOR);
	check->oper = string_to_enum(XCCDF_BOOLOP_MAP, xccdf_attribute_get(reader, XCCDFA_OPERATOR));
	if (xccdf_attribute_get_bool(reader, XCCDFA_NEGATE)) check->oper |= XCCDF_OPERATOR_NOT;

	int depth = xccdf_element_depth(reader) + 1;

	while (xccdf_to_start_element(reader, depth)) {
		switch (xccdf_element_get(reader)) {
			case XCCDFE_CHECK:
			case XCCDFE_COMPLEX_CHECK:
				if (check->oper == 0) break;
				xccdf_list_add(check->children, xccdf_check_new_parse(reader, parent));
				break;
			case XCCDFE_CHECK_CONTENT_REF: {
				const char* href = xccdf_attribute_get(reader, XCCDFA_HREF);
				if (href == NULL) break;
				struct xccdf_check_content_ref* ref = calloc(1, sizeof(struct xccdf_check_content_ref));
				ref->name = xccdf_attribute_copy(reader, XCCDFA_NAME);
				ref->href = strdup(href);
				xccdf_list_add(check->content_refs, ref);
				break;
			}
			case XCCDFE_CHECK_CONTENT:
				if (check->content == NULL)
					check->content = xccdf_element_string_copy(reader);
				break;
			case XCCDFE_CHECK_IMPORT: {
				const char* name = xccdf_attribute_get(reader, XCCDFA_IMPORT_NAME);
				if (name == NULL) break;
				struct xccdf_check_import* imp = calloc(1, sizeof(struct xccdf_check_import));
				imp->name = strdup(name);
				imp->content = xccdf_element_string_copy(reader);
				xccdf_list_add(check->imports, imp);
				break;
			}
			case XCCDFE_CHECK_EXPORT: {
				const char* name = xccdf_attribute_get(reader, XCCDFA_EXPORT_NAME);
				if (name == NULL) break;
				struct xccdf_check_export* exp = calloc(1, sizeof(struct xccdf_check_export));
				exp->name = strdup(name);
				xccdf_benchmark_add_ref(parent->item.benchmark, &exp->value, xccdf_attribute_get(reader, XCCDFA_VALUE_ID), XCCDF_VALUE);
				xccdf_list_add(check->exports, exp);
				break;
			}
			default: break;
		}
		xmlTextReaderRead(reader);
	}

	return check;
}

void xccdf_check_dump(struct xccdf_check* check, int depth)
{
	if (check->id)       { xccdf_print_depth(depth); printf("id      : %s\n", check->id); }
	if (check->system)   { xccdf_print_depth(depth); printf("system  : %s\n", check->system); }
	if (check->selector) { xccdf_print_depth(depth); printf("selector: %s\n", check->selector); }
	if (check->content)  { xccdf_print_depth(depth); printf("content : %s\n", check->content); }
	xccdf_print_depth(depth); printf("content-refs");
	xccdf_list_dump(check->content_refs, (xccdf_dump_func)xccdf_check_content_ref_dump, depth+1);
}

bool xccdf_check_complex(const struct xccdf_check* check)
{
	return check->oper ? true : false;
}

void xccdf_check_delete(struct xccdf_check* check)
{
	if (check) {
		xccdf_list_delete(check->content_refs, (xccdf_destruct_func)xccdf_check_content_ref_delete);
		xccdf_list_delete(check->imports, (xccdf_destruct_func)xccdf_check_import_delete);
		xccdf_list_delete(check->exports, (xccdf_destruct_func)xccdf_check_export_delete);
		xccdf_list_delete(check->children, (xccdf_destruct_func)xccdf_check_delete);
		free(check->id);
		free(check->system);
		free(check->selector);
		free(check->content);
		free(check);
	}
}

void xccdf_check_content_ref_dump(struct xccdf_check_content_ref* ref, int depth)
{
	xccdf_print_depth(depth); printf("%s (%s)\n", ref->href, ref->name);
}

void xccdf_check_content_ref_delete(struct xccdf_check_content_ref* ref)
{
	if (ref) {
		free(ref->name);
		free(ref->href);
		free(ref);
	}
}

void xccdf_check_import_delete(struct xccdf_check_import* item)
{
	if (item) {
		free(item->name);
		free(item->content);
		free(item);
	}
}

void xccdf_check_export_delete(struct xccdf_check_export* item)
{
	if (item) {
		free(item->name);
		free(item);
	}
}

const struct xccdf_string_map XCCDF_STRATEGY_MAP[] = {
	{ XCCDF_STRATEGY_CONFIGURE,    "configure"   },
	{ XCCDF_STRATEGY_COMBINATION,  "combination" },
	{ XCCDF_STRATEGY_DISABLE,      "disable"     },
	{ XCCDF_STRATEGY_ENABLE,       "enable"      },
	{ XCCDF_STRATEGY_PATCH,        "patch"       },
	{ XCCDF_STRATEGY_POLICY,       "policy"      },
	{ XCCDF_STRATEGY_RESTRICT,     "restrict"    },
	{ XCCDF_STRATEGY_UPDATE,       "update"      },
	{ XCCDF_STRATEGY_UNKNOWN,      NULL          }
};

#define XCCDF_FIXCOMMON_PARSE(reader, fix) do { \
	fix->reboot     = xccdf_attribute_get_bool(reader, XCCDFA_REBOOT); \
	fix->strategy   = string_to_enum(XCCDF_STRATEGY_MAP, xccdf_attribute_get(reader, XCCDFA_STRATEGY)); \
	fix->disruption = string_to_enum(XCCDF_LEVEL_MAP, xccdf_attribute_get(reader, XCCDFA_DISRUPTION)); \
	fix->complexity = string_to_enum(XCCDF_LEVEL_MAP, xccdf_attribute_get(reader, XCCDFA_COMPLEXITY)); \
	fix->content    = xccdf_element_string_copy(reader); \
	} while (false)


struct xccdf_fix* xccdf_fix_new_parse(xmlTextReaderPtr reader, struct xccdf_item* parent)
{
	struct xccdf_fix* fix = calloc(1, sizeof(struct xccdf_fix));
	fix->id = xccdf_attribute_copy(reader, XCCDFA_ID);
	if (fix->id != NULL && strcmp(fix->id, "") != 0)
		xccdf_htable_add(parent->item.benchmark->sub.bench.auxdict, fix->id, (void*)fix);
	fix->system = xccdf_attribute_copy(reader, XCCDFA_SYSTEM);
	fix->platform = xccdf_attribute_copy(reader, XCCDFA_PLATFORM);
	XCCDF_FIXCOMMON_PARSE(reader, fix);
	return fix;
}

struct xccdf_fixtext* xccdf_fixtext_new_parse(xmlTextReaderPtr reader, struct xccdf_item* parent)
{
	struct xccdf_fixtext* fix = calloc(1, sizeof(struct xccdf_fixtext));
	// TODO resolve fixref
	xccdf_benchmark_add_ref(parent->item.benchmark, (void*)&fix->fixref, xccdf_attribute_get(reader, XCCDFA_FIXREF), 0);
	XCCDF_FIXCOMMON_PARSE(reader, fix);
	return fix;
}

void xccdf_fixtext_delete(struct xccdf_fixtext* item)
{
	if (item) {
		free(item->content);
		free(item);
	}
}

void xccdf_fix_delete(struct xccdf_fix* item)
{
	if (item) {
		free(item->id);
		free(item->system);
		free(item->platform);
		free(item->content);
		free(item);
	}
}

XCCDF_STATUS_CURRENT(rule)
XCCDF_STATUS_CURRENT(group)

XCCDF_GROUP_IGETTER(item, content)
XCCDF_GROUP_GETTER_I(struct xccdf_group*, extends)

XCCDF_RULE_GETTER(const char*, impact_metric)
XCCDF_RULE_GETTER(enum xccdf_role, role)
XCCDF_RULE_GETTER(enum xccdf_level, severity)
XCCDF_RULE_GETTER(struct xccdf_check*, check)
XCCDF_RULE_IGETTER(ident, idents)
XCCDF_RULE_IGETTER(check, checks)
XCCDF_RULE_IGETTER(profile_note, profile_notes)
XCCDF_RULE_IGETTER(fix, fixes)
XCCDF_RULE_IGETTER(fixtext, fixtexts)
XCCDF_RULE_GETTER_I(struct xccdf_rule*, extends)

XCCDF_GENERIC_GETTER(const char*,              check, id)
XCCDF_GENERIC_GETTER(const char*,              check, system)
XCCDF_GENERIC_GETTER(const char*,              check, selector)
XCCDF_GENERIC_GETTER(const char*,              check, content)
XCCDF_GENERIC_GETTER(struct xccdf_rule*,       check, parent)
XCCDF_GENERIC_GETTER(enum xccdf_bool_operator, check, oper)
XCCDF_GENERIC_IGETTER(check_import,            check, imports)
XCCDF_GENERIC_IGETTER(check_export,            check, exports)
XCCDF_GENERIC_IGETTER(check_content_ref,       check, content_refs)
XCCDF_GENERIC_IGETTER(check,                   check, children)

XCCDF_GENERIC_GETTER(const char*, profile_note, reftag)
XCCDF_GENERIC_GETTER(const char*, profile_note, text)

XCCDF_GENERIC_GETTER(const char*, check_import, name)
XCCDF_GENERIC_GETTER(const char*, check_import, content)

XCCDF_GENERIC_GETTER(const char*, check_export, name)
XCCDF_GENERIC_GETTER(struct xccdf_value*, check_export, value)

XCCDF_GENERIC_GETTER(const char*, check_content_ref, name)
XCCDF_GENERIC_GETTER(const char*, check_content_ref, href)

XCCDF_GENERIC_GETTER(enum xccdf_strategy, fixtext, strategy)
XCCDF_GENERIC_GETTER(enum xccdf_level,    fixtext, disruption)
XCCDF_GENERIC_GETTER(enum xccdf_level,    fixtext, complexity)
XCCDF_GENERIC_GETTER(bool,                fixtext, reboot)
XCCDF_GENERIC_GETTER(const char*,         fixtext, content)
XCCDF_GENERIC_GETTER(struct xccdf_fix*,   fixtext, fixref)

XCCDF_GENERIC_GETTER(enum xccdf_strategy, fix, strategy)
XCCDF_GENERIC_GETTER(enum xccdf_level,    fix, disruption)
XCCDF_GENERIC_GETTER(enum xccdf_level,    fix, complexity)
XCCDF_GENERIC_GETTER(bool,                fix, reboot)
XCCDF_GENERIC_GETTER(const char*,         fix, content)
XCCDF_GENERIC_GETTER(const char*,         fix, system)
XCCDF_GENERIC_GETTER(const char*,         fix, platform)


