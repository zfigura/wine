/*
 * HLSL parser
 *
 * Copyright 2008 Stefan Dösinger
 * Copyright 2012 Matteo Bruni for CodeWeavers
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */
%{
#include "wine/debug.h"

#include <limits.h>
#include <stdio.h>

#include "d3dcompiler_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(hlsl_parser);

int hlsl_lex(void);

struct hlsl_parse_ctx hlsl_ctx;

struct YYLTYPE;
static struct source_location get_location(const struct YYLTYPE *l);

void WINAPIV hlsl_message(const char *fmt, ...)
{
    __ms_va_list args;

    __ms_va_start(args, fmt);
    compilation_message(&hlsl_ctx.messages, fmt, args);
    __ms_va_end(args);
}

static const char *hlsl_get_error_level_name(enum hlsl_error_level level)
{
    static const char * const names[] =
    {
        "error",
        "warning",
        "note",
    };
    return names[level];
}

void WINAPIV hlsl_report_message(const struct source_location loc,
        enum hlsl_error_level level, const char *fmt, ...)
{
    __ms_va_list args;
    char *string = NULL;
    int rc, size = 0;

    while (1)
    {
        __ms_va_start(args, fmt);
        rc = vsnprintf(string, size, fmt, args);
        __ms_va_end(args);

        if (rc >= 0 && rc < size)
            break;

        if (rc >= size)
            size = rc + 1;
        else
            size = size ? size * 2 : 32;

        if (!string)
            string = d3dcompiler_alloc(size);
        else
            string = d3dcompiler_realloc(string, size);
        if (!string)
        {
            ERR("Error reallocating memory for a string.\n");
            return;
        }
    }

    hlsl_message("%s:%u:%u: %s: %s\n", loc.file, loc.line, loc.col,
            hlsl_get_error_level_name(level), string);
    d3dcompiler_free(string);

    if (level == HLSL_LEVEL_ERROR)
        set_parse_status(&hlsl_ctx.status, PARSE_ERR);
    else if (level == HLSL_LEVEL_WARNING)
        set_parse_status(&hlsl_ctx.status, PARSE_WARN);
}

static void hlsl_error(const char *s)
{
    const struct source_location loc =
    {
        .file = hlsl_ctx.source_file,
        .line = hlsl_ctx.line_no,
        .col = hlsl_ctx.column,
    };
    hlsl_report_message(loc, HLSL_LEVEL_ERROR, "%s", s);
}

static void debug_dump_decl(struct hlsl_type *type, DWORD modifiers, const char *declname, unsigned int line_no)
{
    TRACE("Line %u: ", line_no);
    if (modifiers)
        TRACE("%s ", debug_modifiers(modifiers));
    TRACE("%s %s;\n", debug_hlsl_type(type), declname);
}

static void check_invalid_matrix_modifiers(DWORD modifiers, struct source_location loc)
{
    if (modifiers & HLSL_MODIFIERS_MAJORITY_MASK)
    {
        hlsl_report_message(loc, HLSL_LEVEL_ERROR,
                "'row_major' or 'column_major' modifiers are only allowed for matrices");
    }
}

static BOOL type_is_single_reg(const struct hlsl_type *type)
{
    return type->type == HLSL_CLASS_SCALAR || type->type == HLSL_CLASS_VECTOR;
}

static DWORD add_modifiers(DWORD modifiers, DWORD mod, const struct source_location loc)
{
    if (modifiers & mod)
    {
        hlsl_report_message(loc, HLSL_LEVEL_ERROR, "modifier '%s' already specified", debug_modifiers(mod));
        return modifiers;
    }
    if ((mod & HLSL_MODIFIERS_MAJORITY_MASK) && (modifiers & HLSL_MODIFIERS_MAJORITY_MASK))
    {
        hlsl_report_message(loc, HLSL_LEVEL_ERROR, "more than one matrix majority keyword");
        return modifiers;
    }
    return modifiers | mod;
}

static BOOL add_type_to_scope(struct hlsl_scope *scope, struct hlsl_type *def)
{
    if (get_type(scope, def->name, FALSE))
        return FALSE;

    wine_rb_put(&scope->types, def->name, &def->scope_entry);
    return TRUE;
}

static void declare_predefined_types(struct hlsl_scope *scope)
{
    struct hlsl_type *type;
    unsigned int x, y, bt;
    static const char * const names[] =
    {
        "float",
        "half",
        "double",
        "int",
        "uint",
        "bool",
    };
    char name[10];

    static const char *const sampler_names[] =
    {
        "sampler",
        "sampler1D",
        "sampler2D",
        "sampler3D",
        "samplerCUBE"
    };

    for (bt = 0; bt <= HLSL_TYPE_LAST_SCALAR; ++bt)
    {
        for (y = 1; y <= 4; ++y)
        {
            for (x = 1; x <= 4; ++x)
            {
                sprintf(name, "%s%ux%u", names[bt], y, x);
                type = new_hlsl_type(d3dcompiler_strdup(name), HLSL_CLASS_MATRIX, bt, x, y);
                add_type_to_scope(scope, type);

                if (y == 1)
                {
                    sprintf(name, "%s%u", names[bt], x);
                    type = new_hlsl_type(d3dcompiler_strdup(name), HLSL_CLASS_VECTOR, bt, x, y);
                    add_type_to_scope(scope, type);
                    hlsl_ctx.builtin_types.vector[bt][x - 1] = type;

                    if (x == 1)
                    {
                        sprintf(name, "%s", names[bt]);
                        type = new_hlsl_type(d3dcompiler_strdup(name), HLSL_CLASS_SCALAR, bt, x, y);
                        add_type_to_scope(scope, type);
                        hlsl_ctx.builtin_types.scalar[bt] = type;
                    }
                }
            }
        }
    }

    for (bt = 0; bt <= HLSL_SAMPLER_DIM_MAX; ++bt)
    {
        type = new_hlsl_type(d3dcompiler_strdup(sampler_names[bt]), HLSL_CLASS_OBJECT, HLSL_TYPE_SAMPLER, 1, 1);
        type->sampler_dim = bt;
        hlsl_ctx.builtin_types.sampler[bt] = type;
    }

    hlsl_ctx.builtin_types.Void = new_hlsl_type(d3dcompiler_strdup("void"), HLSL_CLASS_OBJECT, HLSL_TYPE_VOID, 1, 1);

    /* DX8 effects predefined types */
    type = new_hlsl_type(d3dcompiler_strdup("DWORD"), HLSL_CLASS_SCALAR, HLSL_TYPE_INT, 1, 1);
    add_type_to_scope(scope, type);
    type = new_hlsl_type(d3dcompiler_strdup("FLOAT"), HLSL_CLASS_SCALAR, HLSL_TYPE_FLOAT, 1, 1);
    add_type_to_scope(scope, type);
    type = new_hlsl_type(d3dcompiler_strdup("VECTOR"), HLSL_CLASS_VECTOR, HLSL_TYPE_FLOAT, 4, 1);
    add_type_to_scope(scope, type);
    type = new_hlsl_type(d3dcompiler_strdup("MATRIX"), HLSL_CLASS_MATRIX, HLSL_TYPE_FLOAT, 4, 4);
    add_type_to_scope(scope, type);
    type = new_hlsl_type(d3dcompiler_strdup("STRING"), HLSL_CLASS_OBJECT, HLSL_TYPE_STRING, 1, 1);
    add_type_to_scope(scope, type);
    type = new_hlsl_type(d3dcompiler_strdup("TEXTURE"), HLSL_CLASS_OBJECT, HLSL_TYPE_TEXTURE, 1, 1);
    add_type_to_scope(scope, type);
    type = new_hlsl_type(d3dcompiler_strdup("PIXELSHADER"), HLSL_CLASS_OBJECT, HLSL_TYPE_PIXELSHADER, 1, 1);
    add_type_to_scope(scope, type);
    type = new_hlsl_type(d3dcompiler_strdup("VERTEXSHADER"), HLSL_CLASS_OBJECT, HLSL_TYPE_VERTEXSHADER, 1, 1);
    add_type_to_scope(scope, type);
}

static BOOL type_is_void(const struct hlsl_type *type)
{
    return type->type == HLSL_CLASS_OBJECT && type->base_type == HLSL_TYPE_VOID;
}

static struct hlsl_ir_if *new_if(struct hlsl_ir_node *condition, struct source_location loc)
{
    struct hlsl_ir_if *iff;

    if (!(iff = d3dcompiler_alloc(sizeof(*iff))))
        return NULL;
    init_node(&iff->node, HLSL_IR_IF, NULL, loc);
    hlsl_src_from_node(&iff->condition, condition);
    list_init(&iff->then_instrs);
    list_init(&iff->else_instrs);
    return iff;
}

static BOOL append_conditional_break(struct list *cond_list)
{
    struct hlsl_ir_node *condition, *not;
    struct hlsl_ir_jump *jump;
    struct hlsl_ir_if *iff;

    /* E.g. "for (i = 0; ; ++i)". */
    if (!list_count(cond_list))
        return TRUE;

    condition = node_from_list(cond_list);
    if (!(not = new_unary_expr(HLSL_IR_UNOP_LOGIC_NOT, condition, condition->loc)))
    {
        ERR("Out of memory.\n");
        return FALSE;
    }
    list_add_tail(cond_list, &not->entry);

    if (!(iff = new_if(not, condition->loc)))
    {
        ERR("Out of memory.\n");
        return FALSE;
    }
    list_add_tail(cond_list, &iff->node.entry);

    if (!(jump = d3dcompiler_alloc(sizeof(*jump))))
    {
        ERR("Out of memory.\n");
        return FALSE;
    }
    init_node(&jump->node, HLSL_IR_JUMP, NULL, condition->loc);
    jump->type = HLSL_IR_JUMP_BREAK;
    list_add_head(&iff->then_instrs, &jump->node.entry);
    return TRUE;
}

enum loop_type
{
    LOOP_FOR,
    LOOP_WHILE,
    LOOP_DO_WHILE
};

static struct list *create_loop(enum loop_type type, struct list *init, struct list *cond,
        struct list *iter, struct list *body, struct source_location loc)
{
    struct list *list = NULL;
    struct hlsl_ir_loop *loop = NULL;
    struct hlsl_ir_if *cond_jump = NULL;

    list = d3dcompiler_alloc(sizeof(*list));
    if (!list)
        goto oom;
    list_init(list);

    if (init)
        list_move_head(list, init);

    loop = d3dcompiler_alloc(sizeof(*loop));
    if (!loop)
        goto oom;
    init_node(&loop->node, HLSL_IR_LOOP, NULL, loc);
    list_add_tail(list, &loop->node.entry);
    list_init(&loop->body);

    if (!append_conditional_break(cond))
        goto oom;

    if (type != LOOP_DO_WHILE)
        list_move_tail(&loop->body, cond);

    list_move_tail(&loop->body, body);

    if (iter)
        list_move_tail(&loop->body, iter);

    if (type == LOOP_DO_WHILE)
        list_move_tail(&loop->body, cond);

    d3dcompiler_free(init);
    d3dcompiler_free(cond);
    d3dcompiler_free(body);
    return list;

oom:
    ERR("Out of memory.\n");
    d3dcompiler_free(loop);
    d3dcompiler_free(cond_jump);
    d3dcompiler_free(list);
    free_instr_list(init);
    free_instr_list(cond);
    free_instr_list(iter);
    free_instr_list(body);
    return NULL;
}

static unsigned int initializer_size(const struct parse_initializer *initializer)
{
    unsigned int count = 0, i;

    for (i = 0; i < initializer->args_count; ++i)
    {
        count += components_count_type(initializer->args[i]->data_type);
    }
    TRACE("Initializer size = %u.\n", count);
    return count;
}

static void free_parse_initializer(struct parse_initializer *initializer)
{
    free_instr_list(initializer->instrs);
    d3dcompiler_free(initializer->args);
}

static struct hlsl_ir_swizzle *new_swizzle(DWORD s, unsigned int components,
        struct hlsl_ir_node *val, struct source_location *loc)
{
    struct hlsl_ir_swizzle *swizzle = d3dcompiler_alloc(sizeof(*swizzle));
    struct hlsl_type *data_type;

    if (!swizzle)
        return NULL;

    if (components == 1)
        data_type = hlsl_ctx.builtin_types.scalar[val->data_type->base_type];
    else
        data_type = hlsl_ctx.builtin_types.vector[val->data_type->base_type][components - 1];

    init_node(&swizzle->node, HLSL_IR_SWIZZLE, data_type, *loc);
    hlsl_src_from_node(&swizzle->val, val);
    swizzle->swizzle = s;
    return swizzle;
}

static struct hlsl_ir_swizzle *get_swizzle(struct hlsl_ir_node *value, const char *swizzle,
        struct source_location *loc)
{
    unsigned int len = strlen(swizzle), component = 0;
    unsigned int i, set, swiz = 0;
    BOOL valid;

    if (value->data_type->type == HLSL_CLASS_MATRIX)
    {
        /* Matrix swizzle */
        BOOL m_swizzle;
        unsigned int inc, x, y;

        if (len < 3 || swizzle[0] != '_')
            return NULL;
        m_swizzle = swizzle[1] == 'm';
        inc = m_swizzle ? 4 : 3;

        if (len % inc || len > inc * 4)
            return NULL;

        for (i = 0; i < len; i += inc)
        {
            if (swizzle[i] != '_')
                return NULL;
            if (m_swizzle)
            {
                if (swizzle[i + 1] != 'm')
                    return NULL;
                y = swizzle[i + 2] - '0';
                x = swizzle[i + 3] - '0';
            }
            else
            {
                y = swizzle[i + 1] - '1';
                x = swizzle[i + 2] - '1';
            }

            if (x >= value->data_type->dimx || y >= value->data_type->dimy)
                return NULL;
            swiz |= (y << 4 | x) << component * 8;
            component++;
        }
        return new_swizzle(swiz, component, value, loc);
    }

    /* Vector swizzle */
    if (len > 4)
        return NULL;

    for (set = 0; set < 2; ++set)
    {
        valid = TRUE;
        component = 0;
        for (i = 0; i < len; ++i)
        {
            char c[2][4] = {{'x', 'y', 'z', 'w'}, {'r', 'g', 'b', 'a'}};
            unsigned int s = 0;

            for (s = 0; s < 4; ++s)
            {
                if (swizzle[i] == c[set][s])
                    break;
            }
            if (s == 4)
            {
                valid = FALSE;
                break;
            }

            if (s >= value->data_type->dimx)
                return NULL;
            swiz |= s << component * 2;
            component++;
        }
        if (valid)
            return new_swizzle(swiz, component, value, loc);
    }

    return NULL;
}

static struct hlsl_ir_var *new_var(const char *name, struct hlsl_type *type, const struct source_location loc,
        const char *semantic, unsigned int modifiers, const struct reg_reservation *reg_reservation)
{
    struct hlsl_ir_var *var;

    if (!(var = d3dcompiler_alloc(sizeof(*var))))
    {
        hlsl_ctx.status = PARSE_ERR;
        return NULL;
    }

    var->name = name;
    var->data_type = type;
    var->loc = loc;
    var->semantic = semantic;
    var->modifiers = modifiers;
    var->reg_reservation = reg_reservation;
    return var;
}

static struct hlsl_ir_var *new_synthetic_var(const char *name, struct hlsl_type *type,
        const struct source_location loc)
{
    struct hlsl_ir_var *var = new_var(strdup(name), type, loc, NULL, 0, NULL);

    if (var)
        list_add_tail(&hlsl_ctx.globals->vars, &var->scope_entry);
    return var;
}

static struct hlsl_ir_assignment *new_assignment(struct hlsl_ir_var *var, struct hlsl_ir_node *offset,
        struct hlsl_ir_node *rhs, unsigned int writemask, struct source_location loc)
{
    struct hlsl_ir_assignment *assign;

    if (!writemask && type_is_single_reg(rhs->data_type))
        writemask = (1 << rhs->data_type->dimx) - 1;

    if (!(assign = d3dcompiler_alloc(sizeof(*assign))))
        return NULL;

    init_node(&assign->node, HLSL_IR_ASSIGNMENT, NULL, loc);
    assign->lhs.var = var;
    hlsl_src_from_node(&assign->lhs.offset, offset);
    hlsl_src_from_node(&assign->rhs, rhs);
    assign->writemask = writemask;
    return assign;
}

static struct hlsl_ir_assignment *new_simple_assignment(struct hlsl_ir_var *lhs, struct hlsl_ir_node *rhs)
{
    return new_assignment(lhs, NULL, rhs, 0, rhs->loc);
}

static struct hlsl_ir_jump *add_return(struct list *instrs,
        struct hlsl_ir_node *return_value, struct source_location loc)
{
    struct hlsl_type *return_type = hlsl_ctx.cur_function->return_type;
    struct hlsl_ir_jump *jump;

    if (return_value)
    {
        struct hlsl_ir_assignment *assignment;

        if (!(return_value = add_implicit_conversion(instrs, return_value, return_type, &loc)))
            return NULL;

        if (!(assignment = new_simple_assignment(hlsl_ctx.cur_function->return_var, return_value)))
            return NULL;
        list_add_after(&return_value->entry, &assignment->node.entry);
    }
    else if (!type_is_void(return_type))
    {
        hlsl_report_message(loc, HLSL_LEVEL_ERROR, "non-void function must return a value");
        return NULL;
    }

    if (!(jump = d3dcompiler_alloc(sizeof(*jump))))
    {
        ERR("Out of memory\n");
        return NULL;
    }
    init_node(&jump->node, HLSL_IR_JUMP, NULL, loc);
    jump->type = HLSL_IR_JUMP_RETURN;
    list_add_tail(instrs, &jump->node.entry);

    return jump;
}

static struct hlsl_ir_constant *new_uint_constant(unsigned int n, const struct source_location loc)
{
    struct hlsl_ir_constant *c;

    if (!(c = d3dcompiler_alloc(sizeof(*c))))
        return NULL;
    init_node(&c->node, HLSL_IR_CONSTANT, hlsl_ctx.builtin_types.scalar[HLSL_TYPE_UINT], loc);
    c->value.u[0] = n;
    return c;
}

struct hlsl_ir_node *new_unary_expr(enum hlsl_ir_expr_op op, struct hlsl_ir_node *arg, struct source_location loc)
{
    struct hlsl_ir_expr *expr;

    if (!(expr = d3dcompiler_alloc(sizeof(*expr))))
        return NULL;
    init_node(&expr->node, HLSL_IR_EXPR, arg->data_type, loc);
    expr->op = op;
    hlsl_src_from_node(&expr->operands[0], arg);
    return &expr->node;
}

struct hlsl_ir_node *new_binary_expr(enum hlsl_ir_expr_op op,
        struct hlsl_ir_node *arg1, struct hlsl_ir_node *arg2)
{
    struct hlsl_ir_expr *expr;

    assert(compare_hlsl_types(arg1->data_type, arg2->data_type));

    if (!(expr = d3dcompiler_alloc(sizeof(*expr))))
        return NULL;
    init_node(&expr->node, HLSL_IR_EXPR, arg1->data_type, arg1->loc);
    expr->op = op;
    hlsl_src_from_node(&expr->operands[0], arg1);
    hlsl_src_from_node(&expr->operands[1], arg2);
    return &expr->node;
}

static struct hlsl_ir_load *new_load(struct hlsl_ir_var *var, struct hlsl_ir_node *offset,
        struct hlsl_type *data_type, const struct source_location loc)
{
    struct hlsl_ir_load *load;

    if (!(load = d3dcompiler_alloc(sizeof(*load))))
        return NULL;
    init_node(&load->node, HLSL_IR_LOAD, data_type, loc);
    load->src.var = var;
    hlsl_src_from_node(&load->src.offset, offset);
    return load;
}

static struct hlsl_ir_load *new_var_load(struct hlsl_ir_var *var, const struct source_location loc)
{
    return new_load(var, NULL, var->data_type, loc);
}

static struct hlsl_ir_load *add_load(struct list *instrs, struct hlsl_ir_node *var_node, struct hlsl_ir_node *offset,
        struct hlsl_type *data_type, const struct source_location loc)
{
    struct hlsl_ir_node *add = NULL;
    struct hlsl_ir_load *load;
    struct hlsl_ir_var *var;

    if (var_node->type == HLSL_IR_LOAD)
    {
        const struct hlsl_deref *src = &load_from_node(var_node)->src;

        var = src->var;
        if (src->offset.node)
        {
            if (!(add = new_binary_expr(HLSL_IR_BINOP_ADD, src->offset.node, offset)))
                return NULL;
            list_add_tail(instrs, &add->entry);
            offset = add;
        }
    }
    else
    {
        struct hlsl_ir_assignment *assign;
        char name[27];

        sprintf(name, "<deref-%p>", var_node);
        if (!(var = new_synthetic_var(name, var_node->data_type, var_node->loc)))
            return NULL;

        TRACE("Synthesized variable %p for %s node.\n", var, debug_node_type(var_node->type));

        if (!(assign = new_simple_assignment(var, var_node)))
            return NULL;

        list_add_tail(instrs, &assign->node.entry);
    }

    if (!(load = d3dcompiler_alloc(sizeof(*load))))
        return NULL;
    init_node(&load->node, HLSL_IR_LOAD, data_type, loc);
    load->src.var = var;
    hlsl_src_from_node(&load->src.offset, offset);
    list_add_tail(instrs, &load->node.entry);
    return load;
}

static struct hlsl_ir_load *add_record_load(struct list *instrs, struct hlsl_ir_node *record,
        const struct hlsl_struct_field *field, const struct source_location loc)
{
    struct hlsl_ir_constant *c;

    if (!(c = new_uint_constant(field->reg_offset * 4, loc)))
        return NULL;
    list_add_tail(instrs, &c->node.entry);

    return add_load(instrs, record, &c->node, field->type, loc);
}

static struct hlsl_ir_load *add_array_load(struct list *instrs, struct hlsl_ir_node *array,
        struct hlsl_ir_node *index, const struct source_location loc)
{
    const struct hlsl_type *expr_type = array->data_type;
    struct hlsl_type *data_type;
    struct hlsl_ir_constant *c;
    struct hlsl_ir_node *mul;

    TRACE("Array load from type %s.\n", debug_hlsl_type(expr_type));

    if (expr_type->type == HLSL_CLASS_ARRAY)
    {
        data_type = expr_type->e.array.type;
    }
    else if (expr_type->type == HLSL_CLASS_MATRIX || expr_type->type == HLSL_CLASS_VECTOR)
    {
        /* This needs to be lowered now, while we still have type information. */
        FIXME("Index of matrix or vector type.\n");
        return NULL;
    }
    else
    {
        if (expr_type->type == HLSL_CLASS_SCALAR)
            hlsl_report_message(loc, HLSL_LEVEL_ERROR, "array-indexed expression is scalar");
        else
            hlsl_report_message(loc, HLSL_LEVEL_ERROR, "expression is not array-indexable");
        return NULL;
    }

    if (!(c = new_uint_constant(data_type->reg_size * 4, loc)))
        return NULL;
    list_add_tail(instrs, &c->node.entry);
    if (!(mul = new_binary_expr(HLSL_IR_BINOP_MUL, index, &c->node)))
        return NULL;
    list_add_tail(instrs, &mul->entry);
    index = mul;

    return add_load(instrs, array, index, data_type, loc);
}

static void struct_var_initializer(struct list *list, struct hlsl_ir_var *var,
        struct parse_initializer *initializer)
{
    struct hlsl_type *type = var->data_type;
    struct hlsl_struct_field *field;
    unsigned int i = 0;

    if (initializer_size(initializer) != components_count_type(type))
    {
        hlsl_report_message(var->loc, HLSL_LEVEL_ERROR, "structure initializer mismatch");
        free_parse_initializer(initializer);
        return;
    }

    list_move_tail(list, initializer->instrs);
    d3dcompiler_free(initializer->instrs);

    LIST_FOR_EACH_ENTRY(field, type->e.elements, struct hlsl_struct_field, entry)
    {
        struct hlsl_ir_node *node = initializer->args[i];
        struct hlsl_ir_assignment *assign;
        struct hlsl_ir_constant *c;

        if (i++ >= initializer->args_count)
            break;

        if (components_count_type(field->type) == components_count_type(node->data_type))
        {
            if (!(c = new_uint_constant(field->reg_offset * 4, node->loc)))
                break;
            list_add_tail(list, &c->node.entry);

            if (!(assign = new_assignment(var, &c->node, node, 0, node->loc)))
                break;
            list_add_tail(list, &assign->node.entry);
        }
        else
            FIXME("Initializing with \"mismatched\" fields is not supported yet.\n");
    }

    d3dcompiler_free(initializer->args);
}

static void free_parse_variable_def(struct parse_variable_def *v)
{
    free_parse_initializer(&v->initializer);
    d3dcompiler_free(v->name);
    d3dcompiler_free((void *)v->semantic);
    d3dcompiler_free(v->reg_reservation);
    d3dcompiler_free(v);
}

static struct list *declare_vars(struct hlsl_type *basic_type, DWORD modifiers, struct list *var_list)
{
    struct hlsl_type *type;
    struct parse_variable_def *v, *v_next;
    struct hlsl_ir_var *var;
    struct list *statements_list = d3dcompiler_alloc(sizeof(*statements_list));
    BOOL local = TRUE;

    if (basic_type->type == HLSL_CLASS_MATRIX)
        assert(basic_type->modifiers & HLSL_MODIFIERS_MAJORITY_MASK);

    if (!statements_list)
    {
        ERR("Out of memory.\n");
        LIST_FOR_EACH_ENTRY_SAFE(v, v_next, var_list, struct parse_variable_def, entry)
            free_parse_variable_def(v);
        d3dcompiler_free(var_list);
        return NULL;
    }
    list_init(statements_list);

    if (!var_list)
        return statements_list;

    LIST_FOR_EACH_ENTRY_SAFE(v, v_next, var_list, struct parse_variable_def, entry)
    {
        if (v->array_size)
            type = new_array_type(basic_type, v->array_size);
        else
            type = basic_type;

        if (!(var = new_var(v->name, type, v->loc, v->semantic, modifiers, v->reg_reservation)))
        {
            free_parse_variable_def(v);
            continue;
        }
        debug_dump_decl(type, modifiers, v->name, v->loc.line);

        if (var->data_type->type != HLSL_CLASS_MATRIX)
            check_invalid_matrix_modifiers(var->modifiers, var->loc);

        if (hlsl_ctx.cur_scope == hlsl_ctx.globals)
        {
            if (!(var->modifiers & HLSL_STORAGE_STATIC))
                var->modifiers |= HLSL_STORAGE_UNIFORM;
            local = FALSE;

            if (find_function(var->name))
                hlsl_report_message(var->loc, HLSL_LEVEL_ERROR, "redefinition of '%s'", var->name);
        }
        else
        {
            static const DWORD invalid = HLSL_STORAGE_EXTERN | HLSL_STORAGE_SHARED
                    | HLSL_STORAGE_GROUPSHARED | HLSL_STORAGE_UNIFORM;
            if (var->modifiers & invalid)
                hlsl_report_message(var->loc, HLSL_LEVEL_ERROR, "modifiers '%s' are invalid for local variables",
                        debug_modifiers(var->modifiers & invalid));

            if (var->semantic)
                hlsl_report_message(var->loc, HLSL_LEVEL_ERROR, "semantics are not allowed on local variables");
        }

        if (type->modifiers & HLSL_MODIFIER_CONST
                && !(var->modifiers & (HLSL_STORAGE_STATIC | HLSL_STORAGE_UNIFORM))
                && !v->initializer.args_count)
        {
            hlsl_report_message(v->loc, HLSL_LEVEL_ERROR, "const variable without initializer");
            free_declaration(var);
            d3dcompiler_free(v);
            continue;
        }

        if (!add_declaration(hlsl_ctx.cur_scope, var, local))
        {
            struct hlsl_ir_var *old = get_variable(hlsl_ctx.cur_scope, var->name);

            hlsl_report_message(var->loc, HLSL_LEVEL_ERROR, "\"%s\" already declared", var->name);
            hlsl_report_message(old->loc, HLSL_LEVEL_NOTE, "\"%s\" was previously declared here", old->name);
        }

        if (v->initializer.args_count)
        {
            unsigned int size = initializer_size(&v->initializer);
            struct hlsl_ir_load *load;

            TRACE("Variable with initializer.\n");
            if (type->type <= HLSL_CLASS_LAST_NUMERIC
                    && type->dimx * type->dimy != size && size != 1)
            {
                if (size < type->dimx * type->dimy)
                {
                    hlsl_report_message(v->loc, HLSL_LEVEL_ERROR,
                            "'%s' initializer does not match", v->name);
                    free_parse_initializer(&v->initializer);
                    d3dcompiler_free(v);
                    continue;
                }
            }
            if ((type->type == HLSL_CLASS_STRUCT || type->type == HLSL_CLASS_ARRAY)
                    && components_count_type(type) != size)
            {
                hlsl_report_message(v->loc, HLSL_LEVEL_ERROR,
                        "'%s' initializer does not match", v->name);
                free_parse_initializer(&v->initializer);
                d3dcompiler_free(v);
                continue;
            }

            if (type->type == HLSL_CLASS_STRUCT)
            {
                struct_var_initializer(statements_list, var, &v->initializer);
                d3dcompiler_free(v);
                continue;
            }
            if (type->type > HLSL_CLASS_LAST_NUMERIC)
            {
                FIXME("Initializers for non scalar/struct variables not supported yet.\n");
                free_parse_initializer(&v->initializer);
                d3dcompiler_free(v);
                continue;
            }
            if (v->array_size > 0)
            {
                FIXME("Initializing arrays is not supported yet.\n");
                free_parse_initializer(&v->initializer);
                d3dcompiler_free(v);
                continue;
            }
            if (v->initializer.args_count > 1)
            {
                FIXME("Complex initializers are not supported yet.\n");
                free_parse_initializer(&v->initializer);
                d3dcompiler_free(v);
                continue;
            }

            load = new_var_load(var, var->loc);
            list_add_tail(v->initializer.instrs, &load->node.entry);
            add_assignment(v->initializer.instrs, &load->node, ASSIGN_OP_ASSIGN, v->initializer.args[0]);
            d3dcompiler_free(v->initializer.args);

            if (modifiers & HLSL_STORAGE_STATIC)
                list_move_tail(&hlsl_ctx.static_initializers, v->initializer.instrs);
            else
                list_move_tail(statements_list, v->initializer.instrs);
            d3dcompiler_free(v->initializer.instrs);
        }
        d3dcompiler_free(v);
    }
    d3dcompiler_free(var_list);
    return statements_list;
}

static BOOL add_struct_field(struct list *fields, struct hlsl_struct_field *field)
{
    struct hlsl_struct_field *f;

    LIST_FOR_EACH_ENTRY(f, fields, struct hlsl_struct_field, entry)
    {
        if (!strcmp(f->name, field->name))
            return FALSE;
    }
    list_add_tail(fields, &field->entry);
    return TRUE;
}

BOOL is_row_major(const struct hlsl_type *type)
{
    /* Default to column-major if the majority isn't explicitly set, which can
     * happen for anonymous nodes. */
    return !!(type->modifiers & HLSL_MODIFIER_ROW_MAJOR);
}

static struct hlsl_type *apply_type_modifiers(struct hlsl_type *type,
        unsigned int *modifiers, struct source_location loc)
{
    unsigned int default_majority = 0;
    struct hlsl_type *new_type;

    /* This function is only used for declarations (i.e. variables and struct
     * fields), which should inherit the matrix majority. We only explicitly set
     * the default majority for declarations—typedefs depend on this—but we
     * want to always set it, so that an hlsl_type object is never used to
     * represent two different majorities (and thus can be used to store its
     * register size, etc.) */
    if (!(*modifiers & HLSL_MODIFIERS_MAJORITY_MASK)
            && !(type->modifiers & HLSL_MODIFIERS_MAJORITY_MASK)
            && type->type == HLSL_CLASS_MATRIX)
    {
        if (hlsl_ctx.matrix_majority == HLSL_COLUMN_MAJOR)
            default_majority = HLSL_MODIFIER_COLUMN_MAJOR;
        else
            default_majority = HLSL_MODIFIER_ROW_MAJOR;
    }

    if (!default_majority && !(*modifiers & HLSL_TYPE_MODIFIERS_MASK))
        return type;

    if (!(new_type = clone_hlsl_type(type, default_majority)))
        return NULL;

    new_type->modifiers = add_modifiers(new_type->modifiers, *modifiers, loc);
    *modifiers &= ~HLSL_TYPE_MODIFIERS_MASK;

    if (new_type->type == HLSL_CLASS_MATRIX)
        new_type->reg_size = is_row_major(new_type) ? new_type->dimy : new_type->dimx;
    return new_type;
}

static struct list *gen_struct_fields(struct hlsl_type *type, DWORD modifiers, struct list *fields)
{
    struct parse_variable_def *v, *v_next;
    struct hlsl_struct_field *field;
    struct list *list;

    if (type->type == HLSL_CLASS_MATRIX)
        assert(type->modifiers & HLSL_MODIFIERS_MAJORITY_MASK);

    list = d3dcompiler_alloc(sizeof(*list));
    if (!list)
    {
        ERR("Out of memory.\n");
        return NULL;
    }
    list_init(list);
    LIST_FOR_EACH_ENTRY_SAFE(v, v_next, fields, struct parse_variable_def, entry)
    {
        debug_dump_decl(type, 0, v->name, v->loc.line);
        field = d3dcompiler_alloc(sizeof(*field));
        if (!field)
        {
            ERR("Out of memory.\n");
            d3dcompiler_free(v);
            return list;
        }
        field->loc = v->loc;
        if (v->array_size)
            field->type = new_array_type(type, v->array_size);
        else
            field->type = type;
        field->name = v->name;
        field->modifiers = modifiers;
        field->semantic = v->semantic;
        if (v->initializer.args_count)
        {
            hlsl_report_message(v->loc, HLSL_LEVEL_ERROR, "struct field with an initializer.\n");
            free_parse_initializer(&v->initializer);
        }
        list_add_tail(list, &field->entry);
        d3dcompiler_free(v);
    }
    d3dcompiler_free(fields);
    return list;
}

static DWORD get_array_size(const struct hlsl_type *type)
{
    if (type->type == HLSL_CLASS_ARRAY)
        return get_array_size(type->e.array.type) * type->e.array.elements_count;
    return 1;
}

static struct hlsl_type *new_struct_type(const char *name, struct list *fields)
{
    struct hlsl_type *type = d3dcompiler_alloc(sizeof(*type));
    struct hlsl_struct_field *field;
    unsigned int reg_size = 0;

    if (!type)
    {
        ERR("Out of memory.\n");
        return NULL;
    }
    type->type = HLSL_CLASS_STRUCT;
    type->base_type = HLSL_TYPE_VOID;
    type->name = name;
    type->dimx = 0;
    type->dimy = 1;
    type->e.elements = fields;

    LIST_FOR_EACH_ENTRY(field, fields, struct hlsl_struct_field, entry)
    {
        field->reg_offset = reg_size;
        reg_size += field->type->reg_size;
        type->dimx += field->type->dimx * field->type->dimy * get_array_size(field->type);
    }
    type->reg_size = reg_size;

    list_add_tail(&hlsl_ctx.types, &type->entry);

    return type;
}

static BOOL add_typedef(DWORD modifiers, struct hlsl_type *orig_type, struct list *list)
{
    BOOL ret;
    struct hlsl_type *type;
    struct parse_variable_def *v, *v_next;

    LIST_FOR_EACH_ENTRY_SAFE(v, v_next, list, struct parse_variable_def, entry)
    {
        if (v->array_size)
            type = new_array_type(orig_type, v->array_size);
        else
            type = clone_hlsl_type(orig_type, 0);
        if (!type)
        {
            ERR("Out of memory\n");
            return FALSE;
        }
        d3dcompiler_free((void *)type->name);
        type->name = v->name;
        type->modifiers |= modifiers;

        if (type->type != HLSL_CLASS_MATRIX)
            check_invalid_matrix_modifiers(type->modifiers, v->loc);
        else
            type->reg_size = is_row_major(type) ? type->dimy : type->dimx;

        if ((type->modifiers & HLSL_MODIFIER_COLUMN_MAJOR)
                && (type->modifiers & HLSL_MODIFIER_ROW_MAJOR))
            hlsl_report_message(v->loc, HLSL_LEVEL_ERROR, "more than one matrix majority keyword");

        ret = add_type_to_scope(hlsl_ctx.cur_scope, type);
        if (!ret)
        {
            hlsl_report_message(v->loc, HLSL_LEVEL_ERROR,
                    "redefinition of custom type '%s'", v->name);
        }
        d3dcompiler_free(v);
    }
    d3dcompiler_free(list);
    return TRUE;
}

static BOOL add_func_parameter(struct list *list, struct parse_parameter *param, const struct source_location loc)
{
    struct hlsl_ir_var *var;

    if (param->type->type == HLSL_CLASS_MATRIX)
        assert(param->type->modifiers & HLSL_MODIFIERS_MAJORITY_MASK);

    if (!(var = new_var(param->name, param->type, loc, param->semantic, param->modifiers, param->reg_reservation)))
        return FALSE;
    var->is_param = TRUE;

    if (!add_declaration(hlsl_ctx.cur_scope, var, FALSE))
    {
        free_declaration(var);
        return FALSE;
    }
    list_add_tail(list, &var->param_entry);
    return TRUE;
}

static struct reg_reservation *parse_reg_reservation(const char *reg_string)
{
    struct reg_reservation *reg_res;
    enum bwritershader_param_register_type type;
    DWORD regnum = 0;

    switch (reg_string[0])
    {
        case 'c':
            type = BWRITERSPR_CONST;
            break;
        case 'i':
            type = BWRITERSPR_CONSTINT;
            break;
        case 'b':
            type = BWRITERSPR_CONSTBOOL;
            break;
        case 's':
            type = BWRITERSPR_SAMPLER;
            break;
        default:
            FIXME("Unsupported register type.\n");
            return NULL;
     }

    if (!sscanf(reg_string + 1, "%u", &regnum))
    {
        FIXME("Unsupported register reservation syntax.\n");
        return NULL;
    }

    reg_res = d3dcompiler_alloc(sizeof(*reg_res));
    if (!reg_res)
    {
        ERR("Out of memory.\n");
        return NULL;
    }
    reg_res->type = type;
    reg_res->regnum = regnum;
    return reg_res;
}

static const struct hlsl_ir_function_decl *get_overloaded_func(struct wine_rb_tree *funcs, char *name,
        struct list *params, BOOL exact_signature)
{
    struct hlsl_ir_function *func;
    struct wine_rb_entry *entry;

    entry = wine_rb_get(funcs, name);
    if (entry)
    {
        func = WINE_RB_ENTRY_VALUE(entry, struct hlsl_ir_function, entry);

        entry = wine_rb_get(&func->overloads, params);
        if (!entry)
        {
            if (!exact_signature)
                FIXME("No exact match, search for a compatible overloaded function (if any).\n");
            return NULL;
        }
        return WINE_RB_ENTRY_VALUE(entry, struct hlsl_ir_function_decl, entry);
    }
    return NULL;
}

static struct hlsl_ir_function_decl *get_func_entry(const char *name)
{
    struct hlsl_ir_function_decl *decl;
    struct hlsl_ir_function *func;
    struct wine_rb_entry *entry;

    if ((entry = wine_rb_get(&hlsl_ctx.functions, name)))
    {
        func = WINE_RB_ENTRY_VALUE(entry, struct hlsl_ir_function, entry);
        WINE_RB_FOR_EACH_ENTRY(decl, &func->overloads, struct hlsl_ir_function_decl, entry)
            return decl;
    }

    return NULL;
}

static struct list *append_unop(struct list *list, struct hlsl_ir_node *node)
{
    list_add_tail(list, &node->entry);
    return list;
}

static struct list *add_binary_expr(struct list *list1, struct list *list2,
        enum hlsl_ir_expr_op op, struct source_location loc)
{
    struct hlsl_ir_node *args[3] = {node_from_list(list1), node_from_list(list2)};
    list_move_tail(list1, list2);
    d3dcompiler_free(list2);
    add_expr(list1, op, args, &loc);
    return list1;
}

static struct list *make_list(struct hlsl_ir_node *node)
{
    struct list *list;

    if (!(list = d3dcompiler_alloc(sizeof(*list))))
    {
        ERR("Out of memory.\n");
        free_instr(node);
        return NULL;
    }
    list_init(list);
    list_add_tail(list, &node->entry);
    return list;
}

static unsigned int evaluate_array_dimension(struct hlsl_ir_node *node)
{
    if (node->data_type->type != HLSL_CLASS_SCALAR)
        return 0;

    switch (node->type)
    {
    case HLSL_IR_CONSTANT:
    {
        struct hlsl_ir_constant *constant = constant_from_node(node);

        switch (constant->node.data_type->base_type)
        {
        case HLSL_TYPE_UINT:
            return constant->value.u[0];
        case HLSL_TYPE_INT:
            return constant->value.i[0];
        case HLSL_TYPE_FLOAT:
            return constant->value.f[0];
        case HLSL_TYPE_DOUBLE:
            return constant->value.d[0];
        case HLSL_TYPE_BOOL:
            return constant->value.b[0];
        default:
            WARN("Invalid type %s.\n", debug_base_type(constant->node.data_type));
            return 0;
        }
    }
    case HLSL_IR_EXPR:
    case HLSL_IR_LOAD:
    case HLSL_IR_SWIZZLE:
        FIXME("Unhandled type %s.\n", debug_node_type(node->type));
        return 0;
    case HLSL_IR_ASSIGNMENT:
    default:
        WARN("Invalid node type %s.\n", debug_node_type(node->type));
        return 0;
    }
}

static struct hlsl_ir_function_decl *new_func_decl(struct hlsl_type *return_type,
        struct list *parameters, const char *semantic, struct source_location loc)
{
    struct hlsl_ir_function_decl *decl;

    if (!(decl = d3dcompiler_alloc(sizeof(*decl))))
        return NULL;
    decl->return_type = return_type;
    decl->parameters = parameters;
    decl->loc = loc;

    if (!type_is_void(return_type))
    {
        struct hlsl_ir_var *return_var;
        char name[28];

        sprintf(name, "<retval-%p>", decl);
        if (!(return_var = new_synthetic_var(name, return_type, loc)))
        {
            d3dcompiler_free(decl);
            return NULL;
        }
        return_var->semantic = semantic;
        return_var->modifiers |= HLSL_STORAGE_OUT;
        decl->return_var = return_var;
    }
    else if (semantic)
        hlsl_report_message(loc, HLSL_LEVEL_ERROR, "void function with a semantic");

    return decl;
}

%}

%locations
%define parse.error verbose
%expect 1

%union
{
    struct hlsl_type *type;
    INT intval;
    FLOAT floatval;
    BOOL boolval;
    char *name;
    DWORD modifiers;
    struct hlsl_ir_node *instr;
    struct list *list;
    struct parse_function function;
    struct parse_parameter parameter;
    struct parse_initializer initializer;
    struct parse_variable_def *variable_def;
    struct parse_if_body if_body;
    enum parse_unary_op unary_op;
    enum parse_assign_op assign_op;
    struct reg_reservation *reg_reservation;
    struct parse_colon_attribute colon_attribute;
}

%token KW_BLENDSTATE
%token KW_BREAK
%token KW_BUFFER
%token KW_CBUFFER
%token KW_COLUMN_MAJOR
%token KW_COMPILE
%token KW_CONST
%token KW_CONTINUE
%token KW_DEPTHSTENCILSTATE
%token KW_DEPTHSTENCILVIEW
%token KW_DISCARD
%token KW_DO
%token KW_DOUBLE
%token KW_ELSE
%token KW_EXTERN
%token KW_FALSE
%token KW_FOR
%token KW_GEOMETRYSHADER
%token KW_GROUPSHARED
%token KW_IF
%token KW_IN
%token KW_INLINE
%token KW_INOUT
%token KW_MATRIX
%token KW_NAMESPACE
%token KW_NOINTERPOLATION
%token KW_OUT
%token KW_PASS
%token KW_PIXELSHADER
%token KW_PRECISE
%token KW_RASTERIZERSTATE
%token KW_RENDERTARGETVIEW
%token KW_RETURN
%token KW_REGISTER
%token KW_ROW_MAJOR
%token KW_SAMPLER
%token KW_SAMPLER1D
%token KW_SAMPLER2D
%token KW_SAMPLER3D
%token KW_SAMPLERCUBE
%token KW_SAMPLER_STATE
%token KW_SAMPLERCOMPARISONSTATE
%token KW_SHARED
%token KW_STATEBLOCK
%token KW_STATEBLOCK_STATE
%token KW_STATIC
%token KW_STRING
%token KW_STRUCT
%token KW_SWITCH
%token KW_TBUFFER
%token KW_TECHNIQUE
%token KW_TECHNIQUE10
%token KW_TEXTURE
%token KW_TEXTURE1D
%token KW_TEXTURE1DARRAY
%token KW_TEXTURE2D
%token KW_TEXTURE2DARRAY
%token KW_TEXTURE2DMS
%token KW_TEXTURE2DMSARRAY
%token KW_TEXTURE3D
%token KW_TEXTURE3DARRAY
%token KW_TEXTURECUBE
%token KW_TRUE
%token KW_TYPEDEF
%token KW_UNIFORM
%token KW_VECTOR
%token KW_VERTEXSHADER
%token KW_VOID
%token KW_VOLATILE
%token KW_WHILE

%token OP_INC
%token OP_DEC
%token OP_AND
%token OP_OR
%token OP_EQ
%token OP_LEFTSHIFT
%token OP_LEFTSHIFTASSIGN
%token OP_RIGHTSHIFT
%token OP_RIGHTSHIFTASSIGN
%token OP_ELLIPSIS
%token OP_LE
%token OP_GE
%token OP_NE
%token OP_ADDASSIGN
%token OP_SUBASSIGN
%token OP_MULASSIGN
%token OP_DIVASSIGN
%token OP_MODASSIGN
%token OP_ANDASSIGN
%token OP_ORASSIGN
%token OP_XORASSIGN
%token OP_UNKNOWN1
%token OP_UNKNOWN2
%token OP_UNKNOWN3
%token OP_UNKNOWN4

%token <intval> PRE_LINE

%token <name> VAR_IDENTIFIER TYPE_IDENTIFIER NEW_IDENTIFIER
%type <name> any_identifier var_identifier
%token <name> STRING
%token <floatval> C_FLOAT
%token <intval> C_INTEGER
%type <boolval> boolean
%type <type> base_type
%type <type> type
%type <list> declaration_statement
%type <list> declaration
%type <list> struct_declaration
%type <type> struct_spec
%type <type> named_struct_spec
%type <type> unnamed_struct_spec
%type <type> field_type
%type <type> typedef_type
%type <list> type_specs
%type <variable_def> type_spec
%type <initializer> complex_initializer
%type <initializer> initializer_expr_list
%type <list> initializer_expr
%type <modifiers> var_modifiers
%type <list> field
%type <list> parameters
%type <list> param_list
%type <list> expr
%type <intval> array
%type <list> statement
%type <list> statement_list
%type <list> compound_statement
%type <list> jump_statement
%type <list> selection_statement
%type <list> loop_statement
%type <function> func_declaration
%type <function> func_prototype
%type <list> fields_list
%type <parameter> parameter
%type <colon_attribute> colon_attribute
%type <name> semantic
%type <reg_reservation> register_opt
%type <variable_def> variable_def
%type <list> variables_def
%type <list> variables_def_optional
%type <if_body> if_body
%type <list> primary_expr
%type <list> postfix_expr
%type <list> unary_expr
%type <list> mul_expr
%type <list> add_expr
%type <list> shift_expr
%type <list> relational_expr
%type <list> equality_expr
%type <list> bitand_expr
%type <list> bitxor_expr
%type <list> bitor_expr
%type <list> logicand_expr
%type <list> logicor_expr
%type <list> conditional_expr
%type <list> assignment_expr
%type <list> expr_statement
%type <unary_op> unary_op
%type <assign_op> assign_op
%type <modifiers> input_mods
%type <modifiers> input_mod
%%

hlsl_prog:                /* empty */
                            {
                            }
                        | hlsl_prog func_declaration
                            {
                                const struct hlsl_ir_function_decl *decl;

                                decl = get_overloaded_func(&hlsl_ctx.functions, $2.name, $2.decl->parameters, TRUE);
                                if (decl && !decl->func->intrinsic)
                                {
                                    if (decl->body && $2.decl->body)
                                    {
                                        hlsl_report_message($2.decl->loc, HLSL_LEVEL_ERROR,
                                                "redefinition of function %s", debugstr_a($2.name));
                                        YYABORT;
                                    }
                                    else if (!compare_hlsl_types(decl->return_type, $2.decl->return_type))
                                    {
                                        hlsl_report_message($2.decl->loc, HLSL_LEVEL_ERROR,
                                                "redefining function %s with a different return type",
                                                debugstr_a($2.name));
                                        hlsl_report_message(decl->loc, HLSL_LEVEL_NOTE,
                                                "%s previously declared here",
                                                debugstr_a($2.name));
                                        YYABORT;
                                    }
                                }

                                TRACE("Adding function '%s' to the function list.\n", $2.name);
                                add_function_decl(&hlsl_ctx.functions, $2.name, $2.decl, FALSE);
                            }
                        | hlsl_prog declaration_statement
                            {
                                TRACE("Declaration statement parsed.\n");

                                if (!list_empty($2))
                                    FIXME("Uniform initializer.\n");
                                free_instr_list($2);
                            }
                        | hlsl_prog preproc_directive
                            {
                            }
                        | hlsl_prog ';'
                            {
                                TRACE("Skipping stray semicolon.\n");
                            }

preproc_directive:        PRE_LINE STRING
                            {
                                const char **new_array = NULL;

                                TRACE("Updating line information to file %s, line %u\n", debugstr_a($2), $1);
                                hlsl_ctx.line_no = $1;
                                if (strcmp($2, hlsl_ctx.source_file))
                                    new_array = d3dcompiler_realloc(hlsl_ctx.source_files,
                                            sizeof(*hlsl_ctx.source_files) * (hlsl_ctx.source_files_count + 1));

                                if (new_array)
                                {
                                    hlsl_ctx.source_files = new_array;
                                    hlsl_ctx.source_files[hlsl_ctx.source_files_count++] = $2;
                                    hlsl_ctx.source_file = $2;
                                }
                                else
                                {
                                    d3dcompiler_free($2);
                                }
                            }

struct_declaration:       var_modifiers struct_spec variables_def_optional ';'
                            {
                                struct hlsl_type *type;
                                DWORD modifiers = $1;

                                if (!$3)
                                {
                                    if (!$2->name)
                                    {
                                        hlsl_report_message(get_location(&@2), HLSL_LEVEL_ERROR,
                                                "anonymous struct declaration with no variables");
                                    }
                                    if (modifiers)
                                    {
                                        hlsl_report_message(get_location(&@1), HLSL_LEVEL_ERROR,
                                                "modifier not allowed on struct type declaration");
                                    }
                                }

                                if (!(type = apply_type_modifiers($2, &modifiers, get_location(&@1))))
                                    YYABORT;
                                $$ = declare_vars(type, modifiers, $3);
                            }

struct_spec:              named_struct_spec
                        | unnamed_struct_spec

named_struct_spec:        KW_STRUCT any_identifier '{' fields_list '}'
                            {
                                BOOL ret;

                                TRACE("Structure %s declaration.\n", debugstr_a($2));
                                $$ = new_struct_type($2, $4);

                                if (get_variable(hlsl_ctx.cur_scope, $2))
                                {
                                    hlsl_report_message(get_location(&@2),
                                            HLSL_LEVEL_ERROR, "redefinition of '%s'", $2);
                                    YYABORT;
                                }

                                ret = add_type_to_scope(hlsl_ctx.cur_scope, $$);
                                if (!ret)
                                {
                                    hlsl_report_message(get_location(&@2),
                                            HLSL_LEVEL_ERROR, "redefinition of struct '%s'", $2);
                                    YYABORT;
                                }
                            }

unnamed_struct_spec:      KW_STRUCT '{' fields_list '}'
                            {
                                TRACE("Anonymous structure declaration.\n");
                                $$ = new_struct_type(NULL, $3);
                            }

any_identifier:           VAR_IDENTIFIER
                        | TYPE_IDENTIFIER
                        | NEW_IDENTIFIER

fields_list:              /* Empty */
                            {
                                $$ = d3dcompiler_alloc(sizeof(*$$));
                                list_init($$);
                            }
                        | fields_list field
                            {
                                BOOL ret;
                                struct hlsl_struct_field *field, *next;

                                $$ = $1;
                                LIST_FOR_EACH_ENTRY_SAFE(field, next, $2, struct hlsl_struct_field, entry)
                                {
                                    ret = add_struct_field($$, field);
                                    if (ret == FALSE)
                                    {
                                        hlsl_report_message(get_location(&@2),
                                                HLSL_LEVEL_ERROR, "redefinition of '%s'", field->name);
                                        d3dcompiler_free(field);
                                    }
                                }
                                d3dcompiler_free($2);
                            }

field_type:               type
                        | unnamed_struct_spec

field:                    var_modifiers field_type variables_def ';'
                            {
                                struct hlsl_type *type;
                                DWORD modifiers = $1;

                                if (!(type = apply_type_modifiers($2, &modifiers, get_location(&@1))))
                                    YYABORT;
                                $$ = gen_struct_fields(type, modifiers, $3);
                            }

func_declaration:         func_prototype compound_statement
                            {
                                TRACE("Function %s parsed.\n", $1.name);
                                $$ = $1;
                                $$.decl->body = $2;
                                pop_scope(&hlsl_ctx);
                            }
                        | func_prototype ';'
                            {
                                TRACE("Function prototype for %s.\n", $1.name);
                                $$ = $1;
                                pop_scope(&hlsl_ctx);
                            }

                        /* var_modifiers is necessary to avoid shift/reduce conflicts. */
func_prototype:           var_modifiers type var_identifier '(' parameters ')' colon_attribute
                            {
                                if ($1)
                                {
                                    hlsl_report_message(get_location(&@1), HLSL_LEVEL_ERROR,
                                            "unexpected modifiers on a function");
                                    YYABORT;
                                }
                                if (get_variable(hlsl_ctx.globals, $3))
                                {
                                    hlsl_report_message(get_location(&@3),
                                            HLSL_LEVEL_ERROR, "redefinition of '%s'\n", $3);
                                    YYABORT;
                                }
                                if (type_is_void($2) && $7.semantic)
                                {
                                    hlsl_report_message(get_location(&@7),
                                            HLSL_LEVEL_ERROR, "void function with a semantic");
                                }

                                if ($7.reg_reservation)
                                {
                                    FIXME("Unexpected register reservation for a function.\n");
                                    d3dcompiler_free($7.reg_reservation);
                                }
                                if (!($$.decl = new_func_decl($2, $5, $7.semantic, get_location(&@3))))
                                {
                                    ERR("Out of memory.\n");
                                    YYABORT;
                                }
                                $$.name = $3;
                                hlsl_ctx.cur_function = $$.decl;
                            }

compound_statement:       '{' '}'
                            {
                                $$ = d3dcompiler_alloc(sizeof(*$$));
                                list_init($$);
                            }
                        | '{' scope_start statement_list '}'
                            {
                                pop_scope(&hlsl_ctx);
                                $$ = $3;
                            }

scope_start:              /* Empty */
                            {
                                push_scope(&hlsl_ctx);
                            }

var_identifier:           VAR_IDENTIFIER
                        | NEW_IDENTIFIER

colon_attribute:          /* Empty */
                            {
                                $$.semantic = NULL;
                                $$.reg_reservation = NULL;
                            }
                        | semantic
                            {
                                $$.semantic = $1;
                                $$.reg_reservation = NULL;
                            }
                        | register_opt
                            {
                                $$.semantic = NULL;
                                $$.reg_reservation = $1;
                            }

semantic:                 ':' any_identifier
                            {
                                $$ = $2;
                            }

                          /* FIXME: Writemasks */
register_opt:             ':' KW_REGISTER '(' any_identifier ')'
                            {
                                $$ = parse_reg_reservation($4);
                                d3dcompiler_free($4);
                            }
                        | ':' KW_REGISTER '(' any_identifier ',' any_identifier ')'
                            {
                                FIXME("Ignoring shader target %s in a register reservation.\n", debugstr_a($4));
                                d3dcompiler_free($4);

                                $$ = parse_reg_reservation($6);
                                d3dcompiler_free($6);
                            }

parameters:               scope_start
                            {
                                $$ = d3dcompiler_alloc(sizeof(*$$));
                                list_init($$);
                            }
                        | scope_start param_list
                            {
                                $$ = $2;
                            }

param_list:               parameter
                            {
                                $$ = d3dcompiler_alloc(sizeof(*$$));
                                list_init($$);
                                if (!add_func_parameter($$, &$1, get_location(&@1)))
                                {
                                    ERR("Error adding function parameter %s.\n", $1.name);
                                    set_parse_status(&hlsl_ctx.status, PARSE_ERR);
                                    YYABORT;
                                }
                            }
                        | param_list ',' parameter
                            {
                                $$ = $1;
                                if (!add_func_parameter($$, &$3, get_location(&@3)))
                                {
                                    hlsl_report_message(get_location(&@3), HLSL_LEVEL_ERROR,
                                            "duplicate parameter %s", $3.name);
                                    YYABORT;
                                }
                            }

parameter:                input_mods var_modifiers type any_identifier colon_attribute
                            {
                                struct hlsl_type *type;
                                DWORD modifiers = $2;

                                if (!(type = apply_type_modifiers($3, &modifiers, get_location(&@2))))
                                    YYABORT;

                                $$.modifiers = $1 ? $1 : HLSL_STORAGE_IN;
                                $$.modifiers |= modifiers;
                                $$.type = type;
                                $$.name = $4;
                                $$.semantic = $5.semantic;
                                $$.reg_reservation = $5.reg_reservation;
                            }

input_mods:               /* Empty */
                            {
                                $$ = 0;
                            }
                        | input_mods input_mod
                            {
                                if ($1 & $2)
                                {
                                    hlsl_report_message(get_location(&@2), HLSL_LEVEL_ERROR,
                                            "duplicate input-output modifiers");
                                    YYABORT;
                                }
                                $$ = $1 | $2;
                            }

input_mod:                KW_IN
                            {
                                $$ = HLSL_STORAGE_IN;
                            }
                        | KW_OUT
                            {
                                $$ = HLSL_STORAGE_OUT;
                            }
                        | KW_INOUT
                            {
                                $$ = HLSL_STORAGE_IN | HLSL_STORAGE_OUT;
                            }

type:

      base_type
        {
            $$ = $1;
        }
    | KW_VECTOR '<' base_type ',' C_INTEGER '>'
        {
            if ($3->type != HLSL_CLASS_SCALAR)
            {
                hlsl_report_message(get_location(&@3), HLSL_LEVEL_ERROR,
                        "vectors of non-scalar types are not allowed\n");
                YYABORT;
            }
            if ($5 < 1 || $5 > 4)
            {
                hlsl_report_message(get_location(&@5), HLSL_LEVEL_ERROR,
                        "vector size must be between 1 and 4\n");
                YYABORT;
            }

            $$ = new_hlsl_type(NULL, HLSL_CLASS_VECTOR, $3->base_type, $5, 1);
        }
    | KW_MATRIX '<' base_type ',' C_INTEGER ',' C_INTEGER '>'
        {
            if ($3->type != HLSL_CLASS_SCALAR)
            {
                hlsl_report_message(get_location(&@3), HLSL_LEVEL_ERROR,
                        "matrices of non-scalar types are not allowed\n");
                YYABORT;
            }
            if ($5 < 1 || $5 > 4)
            {
                hlsl_report_message(get_location(&@5), HLSL_LEVEL_ERROR,
                        "matrix row count must be between 1 and 4\n");
                YYABORT;
            }
            if ($7 < 1 || $7 > 4)
            {
                hlsl_report_message(get_location(&@7), HLSL_LEVEL_ERROR,
                        "matrix column count must be between 1 and 4\n");
                YYABORT;
            }

            $$ = new_hlsl_type(NULL, HLSL_CLASS_MATRIX, $3->base_type, $7, $5);
        }

base_type:

      KW_VOID
        {
            $$ = hlsl_ctx.builtin_types.Void;
        }
    | KW_SAMPLER
        {
            $$ = hlsl_ctx.builtin_types.sampler[HLSL_SAMPLER_DIM_GENERIC];
        }
    | KW_SAMPLER1D
        {
            $$ = hlsl_ctx.builtin_types.sampler[HLSL_SAMPLER_DIM_1D];
        }
    | KW_SAMPLER2D
        {
            $$ = hlsl_ctx.builtin_types.sampler[HLSL_SAMPLER_DIM_2D];
        }
    | KW_SAMPLER3D
        {
            $$ = hlsl_ctx.builtin_types.sampler[HLSL_SAMPLER_DIM_3D];
        }
    | KW_SAMPLERCUBE
        {
            $$ = hlsl_ctx.builtin_types.sampler[HLSL_SAMPLER_DIM_3D];
        }
    | TYPE_IDENTIFIER
        {
            $$ = get_type(hlsl_ctx.cur_scope, $1, TRUE);
            d3dcompiler_free($1);
        }
    | KW_STRUCT TYPE_IDENTIFIER
        {
            $$ = get_type(hlsl_ctx.cur_scope, $2, TRUE);
            if ($$->type != HLSL_CLASS_STRUCT)
                hlsl_report_message(get_location(&@1), HLSL_LEVEL_ERROR, "'%s' redefined as a structure\n", $2);
            d3dcompiler_free($2);
        }

declaration_statement:    declaration
                        | struct_declaration
                        | typedef
                            {
                                $$ = d3dcompiler_alloc(sizeof(*$$));
                                if (!$$)
                                {
                                    ERR("Out of memory\n");
                                    YYABORT;
                                }
                                list_init($$);
                            }

typedef_type:             type
                        | struct_spec

typedef:                  KW_TYPEDEF var_modifiers typedef_type type_specs ';'
                            {
                                if ($2 & ~HLSL_TYPE_MODIFIERS_MASK)
                                {
                                    struct parse_variable_def *v, *v_next;
                                    hlsl_report_message(get_location(&@1),
                                            HLSL_LEVEL_ERROR, "modifier not allowed on typedefs");
                                    LIST_FOR_EACH_ENTRY_SAFE(v, v_next, $4, struct parse_variable_def, entry)
                                        d3dcompiler_free(v);
                                    d3dcompiler_free($4);
                                    YYABORT;
                                }
                                if (!add_typedef($2, $3, $4))
                                    YYABORT;
                            }

type_specs:               type_spec
                            {
                                $$ = d3dcompiler_alloc(sizeof(*$$));
                                list_init($$);
                                list_add_head($$, &$1->entry);
                            }
                        | type_specs ',' type_spec
                            {
                                $$ = $1;
                                list_add_tail($$, &$3->entry);
                            }

type_spec:                any_identifier array
                            {
                                $$ = d3dcompiler_alloc(sizeof(*$$));
                                $$->loc = get_location(&@1);
                                $$->name = $1;
                                $$->array_size = $2;
                            }

declaration:              var_modifiers type variables_def ';'
                            {
                                struct hlsl_type *type;
                                DWORD modifiers = $1;

                                if (!(type = apply_type_modifiers($2, &modifiers, get_location(&@1))))
                                    YYABORT;
                                $$ = declare_vars(type, modifiers, $3);
                            }

variables_def_optional:   /* Empty */
                            {
                                $$ = NULL;
                            }
                        | variables_def
                            {
                                $$ = $1;
                            }

variables_def:            variable_def
                            {
                                $$ = d3dcompiler_alloc(sizeof(*$$));
                                list_init($$);
                                list_add_head($$, &$1->entry);
                            }
                        | variables_def ',' variable_def
                            {
                                $$ = $1;
                                list_add_tail($$, &$3->entry);
                            }

variable_def:             any_identifier array colon_attribute
                            {
                                $$ = d3dcompiler_alloc(sizeof(*$$));
                                $$->loc = get_location(&@1);
                                $$->name = $1;
                                $$->array_size = $2;
                                $$->semantic = $3.semantic;
                                $$->reg_reservation = $3.reg_reservation;
                            }
                        | any_identifier array colon_attribute '=' complex_initializer
                            {
                                TRACE("Declaration with initializer.\n");
                                $$ = d3dcompiler_alloc(sizeof(*$$));
                                $$->loc = get_location(&@1);
                                $$->name = $1;
                                $$->array_size = $2;
                                $$->semantic = $3.semantic;
                                $$->reg_reservation = $3.reg_reservation;
                                $$->initializer = $5;
                            }

array:                    /* Empty */
                            {
                                $$ = 0;
                            }
                        | '[' expr ']'
                            {
                                unsigned int size = evaluate_array_dimension(node_from_list($2));

                                free_instr_list($2);

                                if (!size)
                                {
                                    hlsl_report_message(get_location(&@2), HLSL_LEVEL_ERROR,
                                            "array size is not a positive integer constant\n");
                                    YYABORT;
                                }
                                TRACE("Array size %u.\n", size);

                                if (size > 65536)
                                {
                                    hlsl_report_message(get_location(&@2), HLSL_LEVEL_ERROR,
                                            "array size must be between 1 and 65536");
                                    YYABORT;
                                }
                                $$ = size;
                            }

var_modifiers:            /* Empty */
                            {
                                $$ = 0;
                            }
                        | KW_EXTERN var_modifiers
                            {
                                $$ = add_modifiers($2, HLSL_STORAGE_EXTERN, get_location(&@1));
                            }
                        | KW_NOINTERPOLATION var_modifiers
                            {
                                $$ = add_modifiers($2, HLSL_STORAGE_NOINTERPOLATION, get_location(&@1));
                            }
                        | KW_PRECISE var_modifiers
                            {
                                $$ = add_modifiers($2, HLSL_MODIFIER_PRECISE, get_location(&@1));
                            }
                        | KW_SHARED var_modifiers
                            {
                                $$ = add_modifiers($2, HLSL_STORAGE_SHARED, get_location(&@1));
                            }
                        | KW_GROUPSHARED var_modifiers
                            {
                                $$ = add_modifiers($2, HLSL_STORAGE_GROUPSHARED, get_location(&@1));
                            }
                        | KW_STATIC var_modifiers
                            {
                                $$ = add_modifiers($2, HLSL_STORAGE_STATIC, get_location(&@1));
                            }
                        | KW_UNIFORM var_modifiers
                            {
                                $$ = add_modifiers($2, HLSL_STORAGE_UNIFORM, get_location(&@1));
                            }
                        | KW_VOLATILE var_modifiers
                            {
                                $$ = add_modifiers($2, HLSL_STORAGE_VOLATILE, get_location(&@1));
                            }
                        | KW_CONST var_modifiers
                            {
                                $$ = add_modifiers($2, HLSL_MODIFIER_CONST, get_location(&@1));
                            }
                        | KW_ROW_MAJOR var_modifiers
                            {
                                $$ = add_modifiers($2, HLSL_MODIFIER_ROW_MAJOR, get_location(&@1));
                            }
                        | KW_COLUMN_MAJOR var_modifiers
                            {
                                $$ = add_modifiers($2, HLSL_MODIFIER_COLUMN_MAJOR, get_location(&@1));
                            }

complex_initializer:      initializer_expr
                            {
                                $$.args_count = 1;
                                if (!($$.args = d3dcompiler_alloc(sizeof(*$$.args))))
                                    YYABORT;
                                $$.args[0] = node_from_list($1);
                                $$.instrs = $1;
                            }
                        | '{' initializer_expr_list '}'
                            {
                                $$ = $2;
                            }
                        | '{' initializer_expr_list ',' '}'
                            {
                                $$ = $2;
                            }

initializer_expr:         assignment_expr
                            {
                                $$ = $1;
                            }

initializer_expr_list:    initializer_expr
                            {
                                $$.args_count = 1;
                                if (!($$.args = d3dcompiler_alloc(sizeof(*$$.args))))
                                    YYABORT;
                                $$.args[0] = node_from_list($1);
                                $$.instrs = $1;
                            }
                        | initializer_expr_list ',' initializer_expr
                            {
                                $$ = $1;
                                if (!($$.args = d3dcompiler_realloc($$.args, ($$.args_count + 1) * sizeof(*$$.args))))
                                    YYABORT;
                                $$.args[$$.args_count++] = node_from_list($3);
                                list_move_tail($$.instrs, $3);
                                d3dcompiler_free($3);
                            }

boolean:                  KW_TRUE
                            {
                                $$ = TRUE;
                            }
                        | KW_FALSE
                            {
                                $$ = FALSE;
                            }

statement_list:           statement
                            {
                                $$ = $1;
                            }
                        | statement_list statement
                            {
                                $$ = $1;
                                list_move_tail($$, $2);
                                d3dcompiler_free($2);
                            }

statement:                declaration_statement
                        | expr_statement
                        | compound_statement
                        | jump_statement
                        | selection_statement
                        | loop_statement

jump_statement:

      KW_RETURN expr ';'
        {
            if (!add_return($2, node_from_list($2), get_location(&@1)))
                YYABORT;
            $$ = $2;
        }
    | KW_RETURN ';'
        {
            if (!($$ = d3dcompiler_alloc(sizeof(*$$))))
                YYABORT;
            list_init($$);
            if (!add_return($$, NULL, get_location(&@1)))
                YYABORT;
        }

selection_statement:      KW_IF '(' expr ')' if_body
                            {
                                struct hlsl_ir_node *condition = node_from_list($3);
                                struct hlsl_ir_if *instr;

                                if (!(instr = new_if(condition, get_location(&@1))))
                                    YYABORT;
                                list_move_tail(&instr->then_instrs, $5.then_instrs);
                                list_move_tail(&instr->else_instrs, $5.else_instrs);
                                d3dcompiler_free($5.then_instrs);
                                d3dcompiler_free($5.else_instrs);
                                if (condition->data_type->dimx > 1 || condition->data_type->dimy > 1)
                                {
                                    hlsl_report_message(instr->node.loc, HLSL_LEVEL_ERROR,
                                            "if condition requires a scalar");
                                }
                                $$ = $3;
                                list_add_tail($$, &instr->node.entry);
                            }

if_body:                  statement
                            {
                                $$.then_instrs = $1;
                                $$.else_instrs = NULL;
                            }
                        | statement KW_ELSE statement
                            {
                                $$.then_instrs = $1;
                                $$.else_instrs = $3;
                            }

loop_statement:           KW_WHILE '(' expr ')' statement
                            {
                                $$ = create_loop(LOOP_WHILE, NULL, $3, NULL, $5, get_location(&@1));
                            }
                        | KW_DO statement KW_WHILE '(' expr ')' ';'
                            {
                                $$ = create_loop(LOOP_DO_WHILE, NULL, $5, NULL, $2, get_location(&@1));
                            }
                        | KW_FOR '(' scope_start expr_statement expr_statement expr ')' statement
                            {
                                $$ = create_loop(LOOP_FOR, $4, $5, $6, $8, get_location(&@1));
                                pop_scope(&hlsl_ctx);
                            }
                        | KW_FOR '(' scope_start declaration expr_statement expr ')' statement
                            {
                                if (!$4)
                                    hlsl_report_message(get_location(&@4), HLSL_LEVEL_WARNING,
                                            "no expressions in for loop initializer");
                                $$ = create_loop(LOOP_FOR, $4, $5, $6, $8, get_location(&@1));
                                pop_scope(&hlsl_ctx);
                            }

expr_statement:           ';'
                            {
                                $$ = d3dcompiler_alloc(sizeof(*$$));
                                list_init($$);
                            }
                        | expr ';'
                            {
                                $$ = $1;
                            }

primary_expr:             C_FLOAT
                            {
                                struct hlsl_ir_constant *c = d3dcompiler_alloc(sizeof(*c));
                                if (!c)
                                {
                                    ERR("Out of memory.\n");
                                    YYABORT;
                                }
                                init_node(&c->node, HLSL_IR_CONSTANT,
                                        hlsl_ctx.builtin_types.scalar[HLSL_TYPE_FLOAT], get_location(&@1));
                                c->value.f[0] = $1;
                                if (!($$ = make_list(&c->node)))
                                    YYABORT;
                            }
                        | C_INTEGER
                            {
                                struct hlsl_ir_constant *c = d3dcompiler_alloc(sizeof(*c));
                                if (!c)
                                {
                                    ERR("Out of memory.\n");
                                    YYABORT;
                                }
                                init_node(&c->node, HLSL_IR_CONSTANT,
                                        hlsl_ctx.builtin_types.scalar[HLSL_TYPE_INT], get_location(&@1));
                                c->value.i[0] = $1;
                                if (!($$ = make_list(&c->node)))
                                    YYABORT;
                            }
                        | boolean
                            {
                                struct hlsl_ir_constant *c = d3dcompiler_alloc(sizeof(*c));
                                if (!c)
                                {
                                    ERR("Out of memory.\n");
                                    YYABORT;
                                }
                                init_node(&c->node, HLSL_IR_CONSTANT,
                                        hlsl_ctx.builtin_types.scalar[HLSL_TYPE_BOOL], get_location(&@1));
                                c->value.b[0] = $1;
                                if (!($$ = make_list(&c->node)))
                                    YYABORT;
                            }
                        | VAR_IDENTIFIER
                            {
                                struct hlsl_ir_load *load;
                                struct hlsl_ir_var *var;

                                if (!(var = get_variable(hlsl_ctx.cur_scope, $1)))
                                {
                                    hlsl_report_message(get_location(&@1), HLSL_LEVEL_ERROR,
                                            "variable '%s' is not declared\n", $1);
                                    YYABORT;
                                }
                                if ((load = new_var_load(var, get_location(&@1))))
                                {
                                    if (!($$ = make_list(&load->node)))
                                        YYABORT;
                                }
                                else
                                    $$ = NULL;
                            }
                        | '(' expr ')'
                            {
                                $$ = $2;
                            }

postfix_expr:             primary_expr
                            {
                                $$ = $1;
                            }
                        | postfix_expr OP_INC
                            {
                                struct source_location loc;
                                struct hlsl_ir_node *inc;

                                loc = get_location(&@2);
                                if (node_from_list($1)->data_type->modifiers & HLSL_MODIFIER_CONST)
                                {
                                    hlsl_report_message(loc, HLSL_LEVEL_ERROR, "modifying a const expression");
                                    YYABORT;
                                }
                                inc = new_unary_expr(HLSL_IR_UNOP_POSTINC, node_from_list($1), loc);
                                /* Post increment/decrement expressions are considered const */
                                inc->data_type = clone_hlsl_type(inc->data_type, 0);
                                inc->data_type->modifiers |= HLSL_MODIFIER_CONST;
                                $$ = append_unop($1, inc);
                            }
                        | postfix_expr OP_DEC
                            {
                                struct source_location loc;
                                struct hlsl_ir_node *inc;

                                loc = get_location(&@2);
                                if (node_from_list($1)->data_type->modifiers & HLSL_MODIFIER_CONST)
                                {
                                    hlsl_report_message(loc, HLSL_LEVEL_ERROR, "modifying a const expression");
                                    YYABORT;
                                }
                                inc = new_unary_expr(HLSL_IR_UNOP_POSTDEC, node_from_list($1), loc);
                                /* Post increment/decrement expressions are considered const */
                                inc->data_type = clone_hlsl_type(inc->data_type, 0);
                                inc->data_type->modifiers |= HLSL_MODIFIER_CONST;
                                $$ = append_unop($1, inc);
                            }
                        | postfix_expr '.' any_identifier
                            {
                                struct hlsl_ir_node *node = node_from_list($1);
                                struct source_location loc;

                                loc = get_location(&@2);
                                if (node->data_type->type == HLSL_CLASS_STRUCT)
                                {
                                    struct hlsl_type *type = node->data_type;
                                    struct hlsl_struct_field *field;

                                    $$ = NULL;
                                    LIST_FOR_EACH_ENTRY(field, type->e.elements, struct hlsl_struct_field, entry)
                                    {
                                        if (!strcmp($3, field->name))
                                        {
                                            if (!add_record_load($1, node, field, loc))
                                                YYABORT;
                                            $$ = $1;
                                            break;
                                        }
                                    }
                                    if (!$$)
                                    {
                                        hlsl_report_message(loc, HLSL_LEVEL_ERROR,
                                                "invalid subscript %s", debugstr_a($3));
                                        YYABORT;
                                    }
                                }
                                else if (node->data_type->type <= HLSL_CLASS_LAST_NUMERIC)
                                {
                                    struct hlsl_ir_swizzle *swizzle;

                                    swizzle = get_swizzle(node, $3, &loc);
                                    if (!swizzle)
                                    {
                                        hlsl_report_message(loc, HLSL_LEVEL_ERROR,
                                                "invalid swizzle %s", debugstr_a($3));
                                        YYABORT;
                                    }
                                    $$ = append_unop($1, &swizzle->node);
                                }
                                else
                                {
                                    hlsl_report_message(loc, HLSL_LEVEL_ERROR,
                                            "invalid subscript %s", debugstr_a($3));
                                    YYABORT;
                                }
                            }
    | postfix_expr '[' expr ']'
        {
            struct hlsl_ir_node *array = node_from_list($1), *index = node_from_list($3);

            list_move_tail($1, $3);
            d3dcompiler_free($3);

            if (index->data_type->type != HLSL_CLASS_SCALAR)
            {
                hlsl_report_message(get_location(&@3), HLSL_LEVEL_ERROR, "array index is not scalar");
                free_instr_list($1);
                YYABORT;
            }

            if (!add_array_load($1, array, index, get_location(&@2)))
            {
                free_instr_list($1);
                YYABORT;
            }
            $$ = $1;
        }

      /* "var_modifiers" doesn't make sense in this case, but it's needed
         in the grammar to avoid shift/reduce conflicts. */
    | var_modifiers type '(' initializer_expr_list ')'
        {
            struct hlsl_ir_assignment *assignment;
            unsigned int i, writemask_offset = 0;
            static unsigned int counter;
            struct hlsl_ir_load *load;
            struct hlsl_ir_var *var;
            char name[23];

            if ($1)
            {
                hlsl_report_message(get_location(&@1), HLSL_LEVEL_ERROR,
                        "unexpected modifier on a constructor\n");
                YYABORT;
            }
            if ($2->type > HLSL_CLASS_LAST_NUMERIC)
            {
                hlsl_report_message(get_location(&@2), HLSL_LEVEL_ERROR,
                        "constructors may only be used with numeric data types\n");
                YYABORT;
            }
            if ($2->dimx * $2->dimy != initializer_size(&$4))
            {
                hlsl_report_message(get_location(&@4), HLSL_LEVEL_ERROR,
                        "expected %u components in constructor, but got %u\n",
                        $2->dimx * $2->dimy, initializer_size(&$4));
                YYABORT;
            }

            if ($2->type == HLSL_CLASS_MATRIX)
                FIXME("Matrix constructors are not supported yet.\n");

            sprintf(name, "<constructor-%x>", counter++);
            if (!(var = new_synthetic_var(name, $2, get_location(&@2))))
                YYABORT;
            for (i = 0; i < $4.args_count; ++i)
            {
                struct hlsl_ir_node *arg = $4.args[i];
                struct hlsl_type *data_type;
                unsigned int width;

                if (arg->data_type->type == HLSL_CLASS_OBJECT)
                {
                    hlsl_report_message(arg->loc, HLSL_LEVEL_ERROR,
                            "invalid constructor argument");
                    continue;
                }
                width = components_count_type(arg->data_type);

                if (width > 4)
                {
                    FIXME("Constructor argument with %u components.\n", width);
                    continue;
                }

                if (width == 1)
                    data_type = hlsl_ctx.builtin_types.scalar[$2->base_type];
                else
                    data_type = hlsl_ctx.builtin_types.vector[$2->base_type][width - 1];

                if (!(arg = add_implicit_conversion($4.instrs, arg, data_type, &arg->loc)))
                    continue;

                if (!(assignment = new_assignment(var, NULL, arg,
                        ((1 << width) - 1) << writemask_offset, arg->loc)))
                    YYABORT;
                writemask_offset += width;
                list_add_tail($4.instrs, &assignment->node.entry);
            }
            d3dcompiler_free($4.args);
            if (!(load = new_var_load(var, get_location(&@2))))
                YYABORT;
            $$ = append_unop($4.instrs, &load->node);
        }

unary_expr:               postfix_expr
                            {
                                $$ = $1;
                            }
                        | OP_INC unary_expr
                            {
                                struct source_location loc;

                                loc = get_location(&@1);
                                if (node_from_list($2)->data_type->modifiers & HLSL_MODIFIER_CONST)
                                {
                                    hlsl_report_message(loc, HLSL_LEVEL_ERROR, "modifying a const expression");
                                    YYABORT;
                                }
                                $$ = append_unop($2, new_unary_expr(HLSL_IR_UNOP_PREINC, node_from_list($2), loc));
                            }
                        | OP_DEC unary_expr
                            {
                                struct source_location loc;

                                loc = get_location(&@1);
                                if (node_from_list($2)->data_type->modifiers & HLSL_MODIFIER_CONST)
                                {
                                    hlsl_report_message(loc, HLSL_LEVEL_ERROR, "modifying a const expression");
                                    YYABORT;
                                }
                                $$ = append_unop($2, new_unary_expr(HLSL_IR_UNOP_PREDEC, node_from_list($2), loc));
                            }
                        | unary_op unary_expr
                            {
                                enum hlsl_ir_expr_op ops[] = {0, HLSL_IR_UNOP_NEG,
                                        HLSL_IR_UNOP_LOGIC_NOT, HLSL_IR_UNOP_BIT_NOT};

                                if ($1 == UNARY_OP_PLUS)
                                {
                                    $$ = $2;
                                }
                                else
                                {
                                    $$ = append_unop($2, new_unary_expr(ops[$1], node_from_list($2), get_location(&@1)));
                                }
                            }
                          /* var_modifiers just to avoid shift/reduce conflicts */
                        | '(' var_modifiers type array ')' unary_expr
                            {
                                struct hlsl_type *src_type = node_from_list($6)->data_type;
                                struct hlsl_type *dst_type;
                                struct source_location loc;

                                loc = get_location(&@3);
                                if ($2)
                                {
                                    hlsl_report_message(loc, HLSL_LEVEL_ERROR, "unexpected modifier in a cast");
                                    YYABORT;
                                }

                                if ($4)
                                    dst_type = new_array_type($3, $4);
                                else
                                    dst_type = $3;

                                if (!compatible_data_types(src_type, dst_type))
                                {
                                    hlsl_report_message(loc, HLSL_LEVEL_ERROR, "can't cast from %s to %s",
                                            debug_hlsl_type(src_type), debug_hlsl_type(dst_type));
                                    YYABORT;
                                }

                                $$ = append_unop($6, &new_cast(node_from_list($6), dst_type, &loc)->node);
                            }

unary_op:                 '+'
                            {
                                $$ = UNARY_OP_PLUS;
                            }
                        | '-'
                            {
                                $$ = UNARY_OP_MINUS;
                            }
                        | '!'
                            {
                                $$ = UNARY_OP_LOGICNOT;
                            }
                        | '~'
                            {
                                $$ = UNARY_OP_BITNOT;
                            }

mul_expr:

      unary_expr
    | mul_expr '*' unary_expr
        {
            $$ = add_binary_expr($1, $3, HLSL_IR_BINOP_MUL, get_location(&@2));
        }
    | mul_expr '/' unary_expr
        {
            $$ = add_binary_expr($1, $3, HLSL_IR_BINOP_DIV, get_location(&@2));
        }
    | mul_expr '%' unary_expr
        {
            $$ = add_binary_expr($1, $3, HLSL_IR_BINOP_MOD, get_location(&@2));
        }

add_expr:

      mul_expr
    | add_expr '+' mul_expr
        {
            $$ = add_binary_expr($1, $3, HLSL_IR_BINOP_ADD, get_location(&@2));
        }
    | add_expr '-' mul_expr
        {
            $$ = add_binary_expr($1, $3, HLSL_IR_BINOP_SUB, get_location(&@2));
        }

shift_expr:               add_expr
                            {
                                $$ = $1;
                            }
                        | shift_expr OP_LEFTSHIFT add_expr
                            {
                                FIXME("Left shift\n");
                            }
                        | shift_expr OP_RIGHTSHIFT add_expr
                            {
                                FIXME("Right shift\n");
                            }

relational_expr:

      shift_expr
    | relational_expr '<' shift_expr
        {
            $$ = add_binary_expr($1, $3, HLSL_IR_BINOP_LESS, get_location(&@2));
        }
    | relational_expr '>' shift_expr
        {
            $$ = add_binary_expr($1, $3, HLSL_IR_BINOP_GREATER, get_location(&@2));
        }
    | relational_expr OP_LE shift_expr
        {
            $$ = add_binary_expr($1, $3, HLSL_IR_BINOP_LEQUAL, get_location(&@2));
        }
    | relational_expr OP_GE shift_expr
        {
            $$ = add_binary_expr($1, $3, HLSL_IR_BINOP_GEQUAL, get_location(&@2));
        }

equality_expr:

      relational_expr
    | equality_expr OP_EQ relational_expr
        {
            $$ = add_binary_expr($1, $3, HLSL_IR_BINOP_EQUAL, get_location(&@2));
        }
    | equality_expr OP_NE relational_expr
        {
            $$ = add_binary_expr($1, $3, HLSL_IR_BINOP_NEQUAL, get_location(&@2));
        }

bitand_expr:              equality_expr
                            {
                                $$ = $1;
                            }
                        | bitand_expr '&' equality_expr
                            {
                                FIXME("bitwise AND\n");
                            }

bitxor_expr:              bitand_expr
                            {
                                $$ = $1;
                            }
                        | bitxor_expr '^' bitand_expr
                            {
                                FIXME("bitwise XOR\n");
                            }

bitor_expr:               bitxor_expr
                            {
                                $$ = $1;
                            }
                        | bitor_expr '|' bitxor_expr
                            {
                                FIXME("bitwise OR\n");
                            }

logicand_expr:            bitor_expr
                            {
                                $$ = $1;
                            }
                        | logicand_expr OP_AND bitor_expr
                            {
                                FIXME("logic AND\n");
                            }

logicor_expr:             logicand_expr
                            {
                                $$ = $1;
                            }
                        | logicor_expr OP_OR logicand_expr
                            {
                                FIXME("logic OR\n");
                            }

conditional_expr:         logicor_expr
                            {
                                $$ = $1;
                            }
                        | logicor_expr '?' expr ':' assignment_expr
                            {
                                FIXME("ternary operator\n");
                            }

assignment_expr:

      conditional_expr
    | unary_expr assign_op assignment_expr
        {
            struct hlsl_ir_node *lhs = node_from_list($1), *rhs = node_from_list($3);

            if (lhs->data_type->modifiers & HLSL_MODIFIER_CONST)
            {
                hlsl_report_message(get_location(&@2), HLSL_LEVEL_ERROR, "l-value is const");
                YYABORT;
            }
            list_move_tail($3, $1);
            d3dcompiler_free($1);
            if (!add_assignment($3, lhs, $2, rhs))
                YYABORT;
            $$ = $3;
        }

assign_op:                '='
                            {
                                $$ = ASSIGN_OP_ASSIGN;
                            }
                        | OP_ADDASSIGN
                            {
                                $$ = ASSIGN_OP_ADD;
                            }
                        | OP_SUBASSIGN
                            {
                                $$ = ASSIGN_OP_SUB;
                            }
                        | OP_MULASSIGN
                            {
                                $$ = ASSIGN_OP_MUL;
                            }
                        | OP_DIVASSIGN
                            {
                                $$ = ASSIGN_OP_DIV;
                            }
                        | OP_MODASSIGN
                            {
                                $$ = ASSIGN_OP_MOD;
                            }
                        | OP_LEFTSHIFTASSIGN
                            {
                                $$ = ASSIGN_OP_LSHIFT;
                            }
                        | OP_RIGHTSHIFTASSIGN
                            {
                                $$ = ASSIGN_OP_RSHIFT;
                            }
                        | OP_ANDASSIGN
                            {
                                $$ = ASSIGN_OP_AND;
                            }
                        | OP_ORASSIGN
                            {
                                $$ = ASSIGN_OP_OR;
                            }
                        | OP_XORASSIGN
                            {
                                $$ = ASSIGN_OP_XOR;
                            }

expr:                     assignment_expr
                            {
                                $$ = $1;
                            }
                        | expr ',' assignment_expr
                            {
                                $$ = $1;
                                list_move_tail($$, $3);
                                d3dcompiler_free($3);
                            }

%%

static struct source_location get_location(const struct YYLTYPE *l)
{
    const struct source_location loc =
    {
        .file = hlsl_ctx.source_file,
        .line = l->first_line,
        .col = l->first_column,
    };
    return loc;
}

static void dump_function_decl(struct wine_rb_entry *entry, void *context)
{
    struct hlsl_ir_function_decl *func = WINE_RB_ENTRY_VALUE(entry, struct hlsl_ir_function_decl, entry);
    if (func->body)
        debug_dump_ir_function_decl(func);
}

static void dump_function(struct wine_rb_entry *entry, void *context)
{
    struct hlsl_ir_function *func = WINE_RB_ENTRY_VALUE(entry, struct hlsl_ir_function, entry);
    wine_rb_for_each_entry(&func->overloads, dump_function_decl, NULL);
}

static BOOL transform_ir(BOOL (*func)(struct hlsl_ir_node *, void *), struct list *instrs, void *context)
{
    struct hlsl_ir_node *instr, *next;
    BOOL progress = 0;

    LIST_FOR_EACH_ENTRY_SAFE(instr, next, instrs, struct hlsl_ir_node, entry)
    {
        if (instr->type == HLSL_IR_IF)
        {
            struct hlsl_ir_if *iff = if_from_node(instr);

            progress |= transform_ir(func, &iff->then_instrs, context);
            progress |= transform_ir(func, &iff->else_instrs, context);
        }
        else if (instr->type == HLSL_IR_LOOP)
            progress |= transform_ir(func, &loop_from_node(instr)->body, context);

        progress |= func(instr, context);
    }

    return progress;
}

static void replace_node(struct hlsl_ir_node *old, struct hlsl_ir_node *new)
{
    struct hlsl_src *src, *next;

    LIST_FOR_EACH_ENTRY_SAFE(src, next, &old->uses, struct hlsl_src, entry)
    {
        hlsl_src_remove(src);
        hlsl_src_from_node(src, new);
    }
    list_remove(&old->entry);
    free_instr(old);
}

static BOOL fold_ident(struct hlsl_ir_node *instr, void *context)
{
    if (instr->type == HLSL_IR_EXPR)
    {
        struct hlsl_ir_expr *expr = expr_from_node(instr);

        if (expr->op == HLSL_IR_UNOP_IDENT)
        {
            replace_node(&expr->node, expr->operands[0].node);
            return TRUE;
        }
    }

    return FALSE;
}

static BOOL split_struct_copies(struct hlsl_ir_node *instr, void *context)
{
    const struct hlsl_struct_field *field;
    const struct hlsl_ir_load *rhs_load;
    struct hlsl_ir_assignment *assign;
    const struct hlsl_ir_node *rhs;
    const struct hlsl_type *type;

    if (instr->type != HLSL_IR_ASSIGNMENT)
        return FALSE;

    assign = assignment_from_node(instr);
    rhs = assign->rhs.node;
    type = rhs->data_type;
    if (type->type != HLSL_CLASS_STRUCT)
        return FALSE;

    rhs_load = load_from_node(rhs);

    LIST_FOR_EACH_ENTRY(field, type->e.elements, struct hlsl_struct_field, entry)
    {
        struct hlsl_ir_node *offset, *add;
        struct hlsl_ir_assignment *store;
        struct hlsl_ir_load *field_load;
        struct hlsl_ir_constant *c;

        if (!(c = new_uint_constant(field->reg_offset * 4, instr->loc)))
        {
            hlsl_ctx.status = PARSE_ERR;
            return FALSE;
        }
        list_add_before(&instr->entry, &c->node.entry);

        offset = &c->node;
        if (rhs_load->src.offset.node)
        {
            if (!(add = new_binary_expr(HLSL_IR_BINOP_ADD, rhs_load->src.offset.node, &c->node)))
            {
                hlsl_ctx.status = PARSE_ERR;
                return FALSE;
            }
            list_add_before(&instr->entry, &add->entry);
            offset = add;
        }
        if (!(field_load = d3dcompiler_alloc(sizeof(*field_load))))
        {
            hlsl_ctx.status = PARSE_ERR;
            return FALSE;
        }
        init_node(&field_load->node, HLSL_IR_LOAD, field->type, instr->loc);
        field_load->src.var = rhs_load->src.var;
        hlsl_src_from_node(&field_load->src.offset, offset);
        list_add_before(&instr->entry, &field_load->node.entry);

        offset = &c->node;
        if (assign->lhs.offset.node)
        {
            if (!(add = new_binary_expr(HLSL_IR_BINOP_ADD, assign->lhs.offset.node, &c->node)))
            {
                hlsl_ctx.status = PARSE_ERR;
                return FALSE;
            }
            list_add_before(&instr->entry, &add->entry);
            offset = add;
        }

        if (!(store = new_assignment(assign->lhs.var, offset, &field_load->node, 0, instr->loc)))
        {
            hlsl_ctx.status = PARSE_ERR;
            return FALSE;
        }
        list_add_before(&instr->entry, &store->node.entry);
    }

    /* Remove the assignment instruction, so that we can split structs
     * which contain other structs. Although assignment instructions
     * produce a value, we don't allow HLSL_IR_ASSIGNMENT to be used as
     * a source. */
    list_remove(&assign->node.entry);
    free_instr(&assign->node);
    return TRUE;
}

static BOOL fold_constants(struct hlsl_ir_node *instr, void *context)
{
    struct hlsl_ir_constant *arg1, *arg2 = NULL, *res;
    struct hlsl_ir_expr *expr;
    unsigned int i;

    if (instr->type != HLSL_IR_EXPR)
        return FALSE;
    expr = expr_from_node(instr);

    for (i = 0; i < ARRAY_SIZE(expr->operands); ++i)
    {
        if (expr->operands[i].node && expr->operands[i].node->type != HLSL_IR_CONSTANT)
            return FALSE;
    }
    arg1 = constant_from_node(expr->operands[0].node);
    if (expr->operands[1].node)
        arg2 = constant_from_node(expr->operands[1].node);

    if (!(res = d3dcompiler_alloc(sizeof(*res))))
    {
        hlsl_ctx.status = PARSE_ERR;
        return FALSE;
    }
    init_node(&res->node, HLSL_IR_CONSTANT, instr->data_type, instr->loc);

    switch (instr->data_type->base_type)
    {
        case HLSL_TYPE_UINT:
        {
            unsigned int i;

            switch (expr->op)
            {
                case HLSL_IR_BINOP_ADD:
                    for (i = 0; i < instr->data_type->dimx; ++i)
                        res->value.u[i] = arg1->value.u[i] + arg2->value.u[i];
                    break;

                case HLSL_IR_BINOP_MUL:
                    for (i = 0; i < instr->data_type->dimx; ++i)
                        res->value.u[i] = arg1->value.u[i] * arg2->value.u[i];
                    break;

                default:
                    FIXME("Fold uint expr %#x.\n", expr->op);
                    d3dcompiler_free(res);
                    return FALSE;
            }
            break;
        }

        default:
            FIXME("Fold %s expr %#x.\n", debug_base_type(instr->data_type), expr->op);
            d3dcompiler_free(res);
            return FALSE;
    }

    list_add_before(&expr->node.entry, &res->node.entry);
    replace_node(&expr->node, &res->node);
    return TRUE;
}

static BOOL dce(struct hlsl_ir_node *instr, void *context)
{
    switch (instr->type)
    {
        case HLSL_IR_CONSTANT:
        case HLSL_IR_EXPR:
        case HLSL_IR_LOAD:
        case HLSL_IR_SWIZZLE:
            if (list_empty(&instr->uses))
            {
                list_remove(&instr->entry);
                free_instr(instr);
                return TRUE;
            }
            break;

        case HLSL_IR_ASSIGNMENT:
        {
            struct hlsl_ir_assignment *assignment = assignment_from_node(instr);
            struct hlsl_ir_var *var = assignment->lhs.var;

            if (var->last_read < instr->index)
            {
                list_remove(&instr->entry);
                free_instr(instr);
                return TRUE;
            }
            break;
        }

        case HLSL_IR_IF:
        case HLSL_IR_JUMP:
        case HLSL_IR_LOOP:
            break;
    }

    return FALSE;
}

/* Allocate a unique, ordered index to each instruction, which will be used for
 * computing liveness ranges. */
static unsigned int index_instructions(struct list *instrs, unsigned int index)
{
    struct hlsl_ir_node *instr;

    LIST_FOR_EACH_ENTRY(instr, instrs, struct hlsl_ir_node, entry)
    {
        instr->index = index++;

        if (instr->type == HLSL_IR_IF)
        {
            struct hlsl_ir_if *iff = if_from_node(instr);
            index = index_instructions(&iff->then_instrs, index);
            index = index_instructions(&iff->else_instrs, index);
        }
        else if (instr->type == HLSL_IR_LOOP)
        {
            index = index_instructions(&loop_from_node(instr)->body, index);
            loop_from_node(instr)->next_index = index;
        }
    }

    return index;
}

/* Compute the earliest and latest liveness for each variable. In the case that
 * a variable is accessed inside of a loop, we promote its liveness to extend
 * to at least the range of the entire loop. Note that we don't need to do this
 * for anonymous nodes, since there's currently no way to use a node which was
 * calculated in an earlier iteration of the loop. */
static void compute_liveness_recurse(struct list *instrs, unsigned int loop_first, unsigned int loop_last)
{
    struct hlsl_ir_node *instr;
    struct hlsl_ir_var *var;

    LIST_FOR_EACH_ENTRY(instr, instrs, struct hlsl_ir_node, entry)
    {
        switch (instr->type)
        {
        case HLSL_IR_ASSIGNMENT:
        {
            struct hlsl_ir_assignment *assignment = assignment_from_node(instr);
            var = assignment->lhs.var;
            if (!var->first_write)
                var->first_write = loop_first ? min(instr->index, loop_first) : instr->index;
            assignment->rhs.node->last_read = instr->index;
            if (assignment->lhs.offset.node)
                assignment->lhs.offset.node->last_read = instr->index;
            break;
        }
        case HLSL_IR_EXPR:
        {
            struct hlsl_ir_expr *expr = expr_from_node(instr);
            unsigned int i;

            for (i = 0; i < ARRAY_SIZE(expr->operands) && expr->operands[i].node; ++i)
                expr->operands[i].node->last_read = instr->index;
            break;
        }
        case HLSL_IR_IF:
        {
            struct hlsl_ir_if *iff = if_from_node(instr);
            compute_liveness_recurse(&iff->then_instrs, loop_first, loop_last);
            compute_liveness_recurse(&iff->else_instrs, loop_first, loop_last);
            iff->condition.node->last_read = instr->index;
            break;
        }
        case HLSL_IR_LOAD:
        {
            struct hlsl_ir_load *load = load_from_node(instr);
            var = load->src.var;
            var->last_read = loop_last ? max(instr->index, loop_last) : instr->index;
            if (load->src.offset.node)
                load->src.offset.node->last_read = instr->index;
            break;
        }
        case HLSL_IR_LOOP:
        {
            struct hlsl_ir_loop *loop = loop_from_node(instr);
            compute_liveness_recurse(&loop->body, loop_first ? loop_first : instr->index,
                    loop_last ? loop_last : loop->next_index);
            break;
        }
        case HLSL_IR_SWIZZLE:
        {
            struct hlsl_ir_swizzle *swizzle = swizzle_from_node(instr);
            swizzle->val.node->last_read = instr->index;
            break;
        }
        case HLSL_IR_CONSTANT:
        case HLSL_IR_JUMP:
            break;
        }
    }
}

static void compute_liveness(struct hlsl_ir_function_decl *entry_func)
{
    struct hlsl_scope *scope;
    struct hlsl_ir_var *var;

    /* Index 0 means unused; index 1 means function entry, so start at 2. */
    index_instructions(entry_func->body, 2);

    LIST_FOR_EACH_ENTRY(scope, &hlsl_ctx.scopes, struct hlsl_scope, entry)
    {
        LIST_FOR_EACH_ENTRY(var, &scope->vars, struct hlsl_ir_var, scope_entry)
            var->first_write = var->last_read = 0;
    }

    LIST_FOR_EACH_ENTRY(var, &hlsl_ctx.extern_vars, struct hlsl_ir_var, extern_entry)
    {
        if (var->modifiers & (HLSL_STORAGE_IN | HLSL_STORAGE_UNIFORM))
            var->first_write = 1;
        if (var->modifiers & HLSL_STORAGE_OUT)
            var->last_read = UINT_MAX;
    }

    if (entry_func->return_var)
        entry_func->return_var->last_read = UINT_MAX;

    compute_liveness_recurse(entry_func->body, 0, 0);
}

/* Split uniforms into two variables representing the constant and temp
 * registers, and copy the former to the latter, so that writes to uniforms
 * work. */
static void prepend_uniform_copy(struct list *instrs, struct hlsl_ir_var *var)
{
    struct hlsl_ir_assignment *store;
    struct hlsl_ir_var *const_var;
    struct hlsl_ir_load *load;
    char name[28];

    if (!(const_var = new_var(var->name, var->data_type, var->loc, NULL, var->modifiers, var->reg_reservation)))
    {
        hlsl_ctx.status = PARSE_ERR;
        return;
    }
    const_var->is_param = var->is_param;
    list_add_before(&var->scope_entry, &const_var->scope_entry);
    list_add_tail(&hlsl_ctx.extern_vars, &const_var->extern_entry);
    var->modifiers &= ~(HLSL_STORAGE_UNIFORM | HLSL_STORAGE_IN);
    sprintf(name, "<temp-%.20s>", var->name);
    var->name = strdup(name);

    if (!(load = new_var_load(const_var, var->loc)))
    {
        hlsl_ctx.status = PARSE_ERR;
        return;
    }
    list_add_head(instrs, &load->node.entry);

    if (!(store = new_simple_assignment(var, &load->node)))
    {
        hlsl_ctx.status = PARSE_ERR;
        return;
    }
    list_add_after(&load->node.entry, &store->node.entry);
}

static void prepend_input_copy(struct list *instrs, struct hlsl_ir_var *var,
        struct hlsl_type *type, unsigned int field_offset, const char *semantic)
{
    struct hlsl_ir_assignment *store;
    struct hlsl_ir_constant *offset;
    struct hlsl_ir_var *varying;
    struct hlsl_ir_load *load;
    char name[29];

    sprintf(name, "<input-%.20s>", semantic);
    if (!(varying = new_var(strdup(name), type, var->loc, strdup(semantic), var->modifiers, NULL)))
    {
        hlsl_ctx.status = PARSE_ERR;
        return;
    }
    list_add_before(&var->scope_entry, &varying->scope_entry);
    list_add_tail(&hlsl_ctx.extern_vars, &varying->extern_entry);

    if (!(load = new_var_load(varying, var->loc)))
    {
        hlsl_ctx.status = PARSE_ERR;
        return;
    }
    list_add_head(instrs, &load->node.entry);

    if (!(offset = new_uint_constant(field_offset * 4, var->loc)))
    {
        hlsl_ctx.status = PARSE_ERR;
        return;
    }
    list_add_after(&load->node.entry, &offset->node.entry);

    if (!(store = new_assignment(var, &offset->node, &load->node, 0, var->loc)))
    {
        hlsl_ctx.status = PARSE_ERR;
        return;
    }
    list_add_after(&offset->node.entry, &store->node.entry);
}

static void prepend_input_struct_copy(struct list *instrs, struct hlsl_ir_var *var,
        struct hlsl_type *type, unsigned int field_offset)
{
    struct hlsl_struct_field *field;

    LIST_FOR_EACH_ENTRY(field, type->e.elements, struct hlsl_struct_field, entry)
    {
        if (field->type->type == HLSL_CLASS_STRUCT)
            prepend_input_struct_copy(instrs, var, field->type, field_offset + field->reg_offset);
        else if (field->semantic)
            prepend_input_copy(instrs, var, field->type, field_offset + field->reg_offset, field->semantic);
        else
            hlsl_report_message(field->loc, HLSL_LEVEL_ERROR, "field '%s' is missing a semantic", field->name);
    }
}

/* Split input varyings into two variables representing the varying and temp
 * registers, and copy the former to the latter, so that writes to varyings
 * work. */
static void prepend_input_var_copy(struct list *instrs, struct hlsl_ir_var *var)
{
    if (var->data_type->type == HLSL_CLASS_STRUCT)
        prepend_input_struct_copy(instrs, var, var->data_type, 0);
    else
        prepend_input_copy(instrs, var, var->data_type, 0, var->semantic);

    var->modifiers &= ~HLSL_STORAGE_IN;
}

static void append_output_copy(struct list *instrs, struct hlsl_ir_var *var,
        struct hlsl_type *type, unsigned int field_offset, const char *semantic)
{
    struct hlsl_ir_assignment *store;
    struct hlsl_ir_constant *offset;
    struct hlsl_ir_var *varying;
    struct hlsl_ir_load *load;
    char name[30];

    sprintf(name, "<output-%.20s>", semantic);
    if (!(varying = new_var(strdup(name), type, var->loc, strdup(semantic), var->modifiers, NULL)))
    {
        hlsl_ctx.status = PARSE_ERR;
        return;
    }
    list_add_before(&var->scope_entry, &varying->scope_entry);
    list_add_tail(&hlsl_ctx.extern_vars, &varying->extern_entry);

    if (!(offset = new_uint_constant(field_offset * 4, var->loc)))
    {
        hlsl_ctx.status = PARSE_ERR;
        return;
    }
    list_add_tail(instrs, &offset->node.entry);

    if (!(load = new_load(var, &offset->node, type, var->loc)))
    {
        hlsl_ctx.status = PARSE_ERR;
        return;
    }
    list_add_after(&offset->node.entry, &load->node.entry);

    if (!(store = new_assignment(varying, NULL, &load->node, 0, var->loc)))
    {
        hlsl_ctx.status = PARSE_ERR;
        return;
    }
    list_add_after(&load->node.entry, &store->node.entry);
}

static void append_output_struct_copy(struct list *instrs, struct hlsl_ir_var *var,
        struct hlsl_type *type, unsigned int field_offset)
{
    struct hlsl_struct_field *field;

    LIST_FOR_EACH_ENTRY(field, type->e.elements, struct hlsl_struct_field, entry)
    {
        if (field->type->type == HLSL_CLASS_STRUCT)
            append_output_struct_copy(instrs, var, field->type, field_offset + field->reg_offset);
        else if (field->semantic)
            append_output_copy(instrs, var, field->type, field_offset + field->reg_offset, field->semantic);
        else
            hlsl_report_message(field->loc, HLSL_LEVEL_ERROR, "field '%s' is missing a semantic", field->name);
    }
}

/* Split output varyings into two variables representing the temp and varying
 * registers, and copy the former to the latter, so that reads from output
 * varyings work. */
static void append_output_var_copy(struct list *instrs, struct hlsl_ir_var *var)
{
    if (var->data_type->type == HLSL_CLASS_STRUCT)
        append_output_struct_copy(instrs, var, var->data_type, 0);
    else
        append_output_copy(instrs, var, var->data_type, 0, var->semantic);

    var->modifiers &= ~HLSL_STORAGE_OUT;
}

struct liveness_ctx
{
    unsigned int count;
    struct
    {
        /* 0 if not live yet. */
        unsigned int last_read;
    } *regs;
};

static unsigned char get_available_writemask(struct liveness_ctx *liveness,
        unsigned int first_write, unsigned int index, unsigned int components)
{
    unsigned char i, writemask = 0, count = 0;

    for (i = 0; i < 4; ++i)
    {
        if (liveness->regs[index + i].last_read <= first_write)
        {
            writemask |= 1 << i;
            if (++count == components)
                return writemask;
        }
    }

    return 0;
}

static struct hlsl_reg allocate_register(struct liveness_ctx *liveness,
        unsigned int first_write, unsigned int last_read, unsigned char components)
{
    struct hlsl_reg ret = {.allocated = TRUE};
    unsigned char writemask, i;
    unsigned int regnum;

    for (regnum = 0; regnum < liveness->count; regnum += 4)
    {
        if ((writemask = get_available_writemask(liveness, first_write, regnum, components)))
            break;
    }
    if (regnum == liveness->count)
    {
        if (!array_reserve((void **)&liveness->regs, &liveness->count, regnum + 4, sizeof(*liveness->regs)))
            return ret;
        writemask = (1 << components) - 1;
    }
    for (i = 0; i < 4; ++i)
    {
        if (writemask & (1 << i))
            liveness->regs[regnum + i].last_read = last_read;
    }
    ret.reg = regnum / 4;
    ret.writemask = writemask;
    return ret;
}

static BOOL is_range_available(struct liveness_ctx *liveness, unsigned int first_write,
        unsigned int index, unsigned int elements)
{
    unsigned int i;

    for (i = 0; i < elements; i += 4)
    {
        if (!get_available_writemask(liveness, first_write, index + i, 4))
            return FALSE;
    }
    return TRUE;
}

/* "elements" is the total number of consecutive whole registers needed. */
static struct hlsl_reg allocate_range(struct liveness_ctx *liveness,
        unsigned int first_write, unsigned int last_read, unsigned int elements)
{
    const unsigned int components = elements * 4;
    struct hlsl_reg ret = {.allocated = TRUE};
    unsigned int i, regnum;

    for (regnum = 0; regnum < liveness->count; regnum += 4)
    {
        if (is_range_available(liveness, first_write, regnum, min(components, liveness->count - regnum)))
            break;
    }
    if (!array_reserve((void **)&liveness->regs, &liveness->count, regnum + components, sizeof(*liveness->regs)))
        return ret;

    for (i = 0; i < components; ++i)
        liveness->regs[regnum + i].last_read = last_read;
    ret.reg = regnum / 4;
    return ret;
}

static const char *debugstr_register(char class, struct hlsl_reg reg, const struct hlsl_type *type)
{
    if (type->reg_size > 4)
        return wine_dbg_sprintf("%c%u-%c%u", class, reg.reg, class,
                reg.reg + type->reg_size - 1);
    return wine_dbg_sprintf("%c%u%s", class, reg.reg, debug_writemask(reg.writemask));
}

static void allocate_variable_register(struct hlsl_ir_var *var, struct liveness_ctx *liveness)
{
    /* Variables with special register classes are not allocated here. */
    if (var->modifiers & (HLSL_STORAGE_IN | HLSL_STORAGE_OUT | HLSL_STORAGE_UNIFORM))
        return;

    if (!var->reg.allocated && var->last_read)
    {
        if (var->data_type->reg_size > 1)
            var->reg = allocate_range(liveness, var->first_write,
                    var->last_read, var->data_type->reg_size);
        else
            var->reg = allocate_register(liveness, var->first_write,
                    var->last_read, var->data_type->dimx);
        TRACE("Allocated %s to %s (liveness %u-%u).\n", debugstr_register('r', var->reg, var->data_type),
                var->name, var->first_write, var->last_read);
    }
}

static void allocate_temp_registers_recurse(struct list *instrs, struct liveness_ctx *liveness)
{
    struct hlsl_ir_node *instr;

    LIST_FOR_EACH_ENTRY(instr, instrs, struct hlsl_ir_node, entry)
    {
        if (!instr->reg.allocated && instr->last_read)
        {
            if (instr->data_type->reg_size > 1)
                instr->reg = allocate_range(liveness, instr->index,
                        instr->last_read, instr->data_type->reg_size);
            else
                instr->reg = allocate_register(liveness, instr->index,
                        instr->last_read, instr->data_type->dimx);
            TRACE("Allocated %s to anonymous expression @%u (liveness %u-%u).\n",
                    debugstr_register('r', instr->reg, instr->data_type),
                    instr->index, instr->index, instr->last_read);
        }

        switch (instr->type)
        {
        case HLSL_IR_ASSIGNMENT:
        {
            struct hlsl_ir_assignment *assignment = assignment_from_node(instr);
            allocate_variable_register(assignment->lhs.var, liveness);
            break;
        }
        case HLSL_IR_IF:
        {
            struct hlsl_ir_if *iff = if_from_node(instr);
            allocate_temp_registers_recurse(&iff->then_instrs, liveness);
            allocate_temp_registers_recurse(&iff->else_instrs, liveness);
            break;
        }
        case HLSL_IR_LOAD:
        {
            struct hlsl_ir_load *load = load_from_node(instr);
            allocate_variable_register(load->src.var, liveness);
            break;
        }
        case HLSL_IR_LOOP:
        {
            struct hlsl_ir_loop *loop = loop_from_node(instr);
            allocate_temp_registers_recurse(&loop->body, liveness);
            break;
        }
        default:
            break;
        }
    }
}

/* Simple greedy temporary register allocation pass that just assigns a unique
 * index to all (simultaneously live) variables or intermediate values. Agnostic
 * as to how many registers are actually available for the current backend, and
 * does not handle constants. */
static void allocate_temp_registers(struct hlsl_ir_function_decl *entry_func)
{
    struct liveness_ctx liveness = {0};
    allocate_temp_registers_recurse(entry_func->body, &liveness);
}

struct vec4
{
    float f[4];
};

struct constant_defs
{
    struct vec4 *values;
    unsigned int count, size;
};

static void allocate_const_registers_recurse(struct list *instrs, struct liveness_ctx *ctx,
        struct constant_defs *defs)
{
    struct hlsl_ir_node *instr;

    LIST_FOR_EACH_ENTRY(instr, instrs, struct hlsl_ir_node, entry)
    {
        switch (instr->type)
        {
        case HLSL_IR_CONSTANT:
        {
            struct hlsl_ir_constant *constant = constant_from_node(instr);
            const struct hlsl_type *type = instr->data_type;
            unsigned int reg_size = type->reg_size, x, y, i, writemask;

            if (reg_size > 1)
                constant->reg = allocate_range(ctx, 1, INT_MAX, reg_size);
            else
                constant->reg = allocate_register(ctx, 1, INT_MAX, type->dimx);
            TRACE("Allocated %s to ", debugstr_register('c', constant->reg, type));
            if (TRACE_ON(hlsl_parser))
                debug_dump_ir_constant(constant);
            TRACE(".\n");

            if (!array_reserve((void **)&defs->values, &defs->size,
                    constant->reg.reg + reg_size, sizeof(*defs->values)))
            {
                hlsl_ctx.status = PARSE_ERR;
                return;
            }
            defs->count = constant->reg.reg + reg_size;

            assert(type->type <= HLSL_CLASS_LAST_NUMERIC);

            if (!(writemask = constant->reg.writemask))
                writemask = (1 << type->dimx) - 1;

            for (y = 0; y < type->dimy; ++y)
            {
                for (x = 0, i = 0; x < 4; ++x)
                {
                    float f;

                    if (!(writemask & (1 << x)))
                        continue;

                    switch (type->base_type)
                    {
                        case HLSL_TYPE_BOOL:
                            f = constant->value.b[i++];
                            break;

                        case HLSL_TYPE_FLOAT:
                            f = constant->value.f[i++];
                            break;

                        case HLSL_TYPE_INT:
                            f = constant->value.i[i++];
                            break;

                        case HLSL_TYPE_UINT:
                            f = constant->value.u[i++];
                            break;

                        default:
                            FIXME("Unhandled type %s.\n", debug_base_type(type));
                            return;
                    }
                    defs->values[constant->reg.reg + y].f[x] = f;
                }
            }

            break;
        }
        case HLSL_IR_IF:
        {
            struct hlsl_ir_if *iff = if_from_node(instr);
            allocate_const_registers_recurse(&iff->then_instrs, ctx, defs);
            allocate_const_registers_recurse(&iff->else_instrs, ctx, defs);
            break;
        }
        case HLSL_IR_LOOP:
        {
            struct hlsl_ir_loop *loop = loop_from_node(instr);
            allocate_const_registers_recurse(&loop->body, ctx, defs);
            break;
        }
        default:
            break;
        }
    }
}

static struct constant_defs allocate_const_registers(struct hlsl_ir_function_decl *entry_func)
{
    struct constant_defs defs = {0};
    struct liveness_ctx ctx = {0};
    struct hlsl_ir_var *var;

    allocate_const_registers_recurse(entry_func->body, &ctx, &defs);

    LIST_FOR_EACH_ENTRY(var, &hlsl_ctx.extern_vars, struct hlsl_ir_var, extern_entry)
    {
        if ((var->modifiers & HLSL_STORAGE_UNIFORM) && var->last_read)
        {
            if (var->data_type->reg_size > 1)
                var->reg = allocate_range(&ctx, 1, INT_MAX, var->data_type->reg_size);
            else
            {
                var->reg = allocate_register(&ctx, 1, INT_MAX, 4);
                var->reg.writemask = (1 << var->data_type->dimx) - 1;
            }
            TRACE("Allocated %s to %s.\n", debugstr_register('c', var->reg, var->data_type), var->name);
        }
    }

    return defs;
}

struct bytecode_buffer
{
    DWORD *data;
    unsigned int count, size;
    HRESULT status;
};

static void put_dword(struct bytecode_buffer *buffer, DWORD value)
{
    if (buffer->status)
        return;

    if (!array_reserve((void **)&buffer->data, &buffer->size, buffer->count + 1, sizeof(*buffer->data)))
    {
        buffer->status = E_OUTOFMEMORY;
        return;
    }
    buffer->data[buffer->count++] = value;
}

static void set_dword(struct bytecode_buffer *buffer, unsigned int index, DWORD value)
{
    if (buffer->status)
        return;

    assert(index < buffer->count);
    buffer->data[index] = value;
}

static void put_string(struct bytecode_buffer *buffer, const char *str)
{
    unsigned int len = (strlen(str) + 1 + 3) / sizeof(DWORD);

    if (buffer->status)
        return;

    if (!array_reserve((void **)&buffer->data, &buffer->size, buffer->count + len, sizeof(*buffer->data)))
    {
        buffer->status = E_OUTOFMEMORY;
        return;
    }

    strcpy((char *)(buffer->data + buffer->count), str);
    buffer->count += len;
}

static DWORD sm1_version(enum shader_type type, unsigned int major, unsigned int minor)
{
    if (type == ST_VERTEX)
        return D3DVS_VERSION(major, minor);
    else
        return D3DPS_VERSION(major, minor);
}

static D3DXPARAMETER_CLASS sm1_class(const struct hlsl_type *type)
{
    switch (type->type)
    {
        case HLSL_CLASS_ARRAY:
            return sm1_class(type->e.array.type);
        case HLSL_CLASS_MATRIX:
            if (type->modifiers & HLSL_MODIFIER_COLUMN_MAJOR)
                return D3DXPC_MATRIX_COLUMNS;
            if (type->modifiers & HLSL_MODIFIER_ROW_MAJOR)
                return D3DXPC_MATRIX_ROWS;
            return (hlsl_ctx.matrix_majority == HLSL_COLUMN_MAJOR) ? D3DXPC_MATRIX_COLUMNS : D3DXPC_MATRIX_ROWS;
        case HLSL_CLASS_OBJECT:
            return D3DXPC_OBJECT;
        case HLSL_CLASS_SCALAR:
            return D3DXPC_SCALAR;
        case HLSL_CLASS_STRUCT:
            return D3DXPC_STRUCT;
        case HLSL_CLASS_VECTOR:
            return D3DXPC_VECTOR;
        default:
            ERR("Invalid class %#x.\n", type->type);
            assert(0);
            return 0;
    }
}

static D3DXPARAMETER_TYPE sm1_base_type(const struct hlsl_type *type)
{
    switch (type->base_type)
    {
        case HLSL_TYPE_BOOL:
            return D3DXPT_BOOL;
        case HLSL_TYPE_FLOAT:
        case HLSL_TYPE_HALF:
            return D3DXPT_FLOAT;
        case HLSL_TYPE_INT:
        case HLSL_TYPE_UINT:
            return D3DXPT_INT;
        case HLSL_TYPE_PIXELSHADER:
            return D3DXPT_PIXELSHADER;
        case HLSL_TYPE_SAMPLER:
            switch (type->sampler_dim)
            {
                case HLSL_SAMPLER_DIM_1D:
                    return D3DXPT_SAMPLER1D;
                case HLSL_SAMPLER_DIM_2D:
                    return D3DXPT_SAMPLER2D;
                case HLSL_SAMPLER_DIM_3D:
                    return D3DXPT_SAMPLER3D;
                case HLSL_SAMPLER_DIM_CUBE:
                    return D3DXPT_SAMPLERCUBE;
                case HLSL_SAMPLER_DIM_GENERIC:
                    return D3DXPT_SAMPLER;
                default:
                    ERR("Invalid dimension %#x.\n", type->sampler_dim);
            }
            break;
        case HLSL_TYPE_STRING:
            return D3DXPT_STRING;
        case HLSL_TYPE_TEXTURE:
            switch (type->sampler_dim)
            {
                case HLSL_SAMPLER_DIM_1D:
                    return D3DXPT_TEXTURE1D;
                case HLSL_SAMPLER_DIM_2D:
                    return D3DXPT_TEXTURE2D;
                case HLSL_SAMPLER_DIM_3D:
                    return D3DXPT_TEXTURE3D;
                case HLSL_SAMPLER_DIM_CUBE:
                    return D3DXPT_TEXTURECUBE;
                case HLSL_SAMPLER_DIM_GENERIC:
                    return D3DXPT_TEXTURE;
                default:
                    ERR("Invalid dimension %#x.\n", type->sampler_dim);
            }
            break;
        case HLSL_TYPE_VERTEXSHADER:
            return D3DXPT_VERTEXSHADER;
        case HLSL_TYPE_VOID:
            return D3DXPT_VOID;
        default:
            ERR("Invalid type %s.\n", debug_base_type(type));
    }
    assert(0);
    return 0;
}

static const struct hlsl_type *get_array_type(const struct hlsl_type *type)
{
    if (type->type == HLSL_CLASS_ARRAY)
        return get_array_type(type->e.array.type);
    return type;
}

static void write_sm1_type(struct bytecode_buffer *buffer, struct hlsl_type *type, unsigned int ctab_start)
{
    const struct hlsl_type *array_type = get_array_type(type);
    DWORD fields_offset = 0, field_count = 0;
    DWORD array_size = get_array_size(type);
    struct hlsl_struct_field *field;

    if (type->bytecode_offset)
        return;

    if (array_type->type == HLSL_CLASS_STRUCT)
    {
        LIST_FOR_EACH_ENTRY(field, array_type->e.elements, struct hlsl_struct_field, entry)
        {
            field->name_offset = buffer->count;
            put_string(buffer, field->name);
            write_sm1_type(buffer, field->type, ctab_start);
        }

        fields_offset = (buffer->count - ctab_start) * sizeof(DWORD);

        LIST_FOR_EACH_ENTRY(field, array_type->e.elements, struct hlsl_struct_field, entry)
        {
            put_dword(buffer, (field->name_offset - ctab_start) * sizeof(DWORD));
            put_dword(buffer, (field->type->bytecode_offset - ctab_start) * sizeof(DWORD));
            ++field_count;
        }
    }

    type->bytecode_offset = buffer->count;
    put_dword(buffer, sm1_class(type) | (sm1_base_type(type) << 16));
    put_dword(buffer, type->dimy | (type->dimx << 16));
    put_dword(buffer, array_size | (field_count << 16));
    put_dword(buffer, fields_offset);
}

static void write_sm1_uniforms(struct bytecode_buffer *buffer, struct hlsl_ir_function_decl *entry_func)
{
    unsigned int ctab_start, vars_start;
    unsigned int uniform_count = 0;
    const struct hlsl_ir_var *var;

    LIST_FOR_EACH_ENTRY(var, &hlsl_ctx.extern_vars, struct hlsl_ir_var, extern_entry)
    {
        if ((var->modifiers & HLSL_STORAGE_UNIFORM) && var->reg.allocated)
            ++uniform_count;
    }

    put_dword(buffer, 0); /* COMMENT tag + size */
    put_dword(buffer, MAKEFOURCC('C','T','A','B'));

    ctab_start = buffer->count;

    put_dword(buffer, sizeof(D3DXSHADER_CONSTANTTABLE)); /* size of this header */
    put_dword(buffer, 0); /* creator */
    put_dword(buffer, sm1_version(hlsl_ctx.shader_type, hlsl_ctx.major_version, hlsl_ctx.minor_version));
    put_dword(buffer, uniform_count);
    put_dword(buffer, sizeof(D3DXSHADER_CONSTANTTABLE)); /* offset of constants */
    put_dword(buffer, 0); /* FIXME: flags */
    put_dword(buffer, 0); /* FIXME: target string */

    vars_start = buffer->count;

    LIST_FOR_EACH_ENTRY(var, &hlsl_ctx.extern_vars, struct hlsl_ir_var, extern_entry)
    {
        if ((var->modifiers & HLSL_STORAGE_UNIFORM) && var->reg.allocated)
        {
            put_dword(buffer, 0); /* name */
            put_dword(buffer, D3DXRS_FLOAT4 | (var->reg.reg << 16));
            put_dword(buffer, var->data_type->reg_size);
            put_dword(buffer, 0); /* type */
            put_dword(buffer, 0); /* FIXME: default value */
        }
    }

    uniform_count = 0;

    LIST_FOR_EACH_ENTRY(var, &hlsl_ctx.extern_vars, struct hlsl_ir_var, extern_entry)
    {
        if ((var->modifiers & HLSL_STORAGE_UNIFORM) && var->reg.allocated)
        {
            set_dword(buffer, vars_start + (uniform_count * 5), (buffer->count - ctab_start) * sizeof(DWORD));

            if (var->is_param)
            {
                char *name;

                if (!(name = d3dcompiler_alloc(strlen(var->name) + 2)))
                {
                    buffer->status = E_OUTOFMEMORY;
                    return;
                }
                name[0] = '$';
                strcpy(name + 1, var->name);
                put_string(buffer, name);
                d3dcompiler_free(name);
            }
            else
            {
                put_string(buffer, var->name);
            }

            write_sm1_type(buffer, var->data_type, ctab_start);
            set_dword(buffer, vars_start + (uniform_count * 5) + 3,
                    (var->data_type->bytecode_offset - ctab_start) * sizeof(DWORD));
            ++uniform_count;
        }
    }

    set_dword(buffer, ctab_start + 1, (buffer->count - ctab_start) * sizeof(DWORD));
    put_string(buffer, "Wine 'Bazman' HLSL shader compiler");

    set_dword(buffer, ctab_start - 2, D3DSIO_COMMENT | ((buffer->count - (ctab_start - 1)) << 16));
}

static DWORD sm1_encode_register_type(D3DSHADER_PARAM_REGISTER_TYPE type)
{
    return ((type << D3DSP_REGTYPE_SHIFT) & D3DSP_REGTYPE_MASK)
            | ((type << D3DSP_REGTYPE_SHIFT2) & D3DSP_REGTYPE_MASK2);
}

static void write_sm1_constant_defs(struct bytecode_buffer *buffer, struct constant_defs *defs)
{
    unsigned int i, x;

    for (i = 0; i < defs->count; ++i)
    {
        DWORD token = D3DSIO_DEF;

        if (hlsl_ctx.major_version > 1)
            token |= 5 << D3DSI_INSTLENGTH_SHIFT;
        put_dword(buffer, token);

        token = (1u << 31);
        token |= sm1_encode_register_type(D3DSPR_CONST);
        token |= D3DSP_WRITEMASK_ALL;
        token |= i;
        put_dword(buffer, token);
        for (x = 0; x < 4; ++x)
        {
            union
            {
                float f;
                DWORD d;
            } u;
            u.f = defs->values[i].f[x];
            put_dword(buffer, u.d);
        }
    }
}

static HRESULT write_sm1_shader(struct hlsl_ir_function_decl *entry_func,
        struct constant_defs *constant_defs, ID3D10Blob **shader_blob)
{
    struct bytecode_buffer buffer = {0};
    HRESULT hr;

    put_dword(&buffer, sm1_version(hlsl_ctx.shader_type, hlsl_ctx.major_version, hlsl_ctx.minor_version));

    write_sm1_uniforms(&buffer, entry_func);

    write_sm1_constant_defs(&buffer, constant_defs);

    put_dword(&buffer, D3DSIO_END);

    if (SUCCEEDED(hr = buffer.status))
    {
        if (SUCCEEDED(hr = D3DCreateBlob(buffer.count * sizeof(DWORD), shader_blob)))
            memcpy(ID3D10Blob_GetBufferPointer(*shader_blob), buffer.data, buffer.count * sizeof(DWORD));
    }
    d3dcompiler_free(buffer.data);
    return hr;
}

HRESULT parse_hlsl(enum shader_type type, DWORD major, DWORD minor,
        const char *entrypoint, ID3D10Blob **shader_blob, char **messages)
{
    struct hlsl_ir_function_decl *entry_func;
    struct hlsl_scope *scope, *next_scope;
    struct hlsl_type *hlsl_type, *next_type;
    struct hlsl_ir_var *var, *next_var;
    HRESULT hr = E_FAIL;
    unsigned int i;

    hlsl_ctx.shader_type = type;
    hlsl_ctx.major_version = major;
    hlsl_ctx.minor_version = minor;
    hlsl_ctx.status = PARSE_SUCCESS;
    hlsl_ctx.messages.size = hlsl_ctx.messages.capacity = 0;
    hlsl_ctx.line_no = hlsl_ctx.column = 1;
    hlsl_ctx.source_file = d3dcompiler_strdup("");
    hlsl_ctx.source_files = d3dcompiler_alloc(sizeof(*hlsl_ctx.source_files));
    if (hlsl_ctx.source_files)
        hlsl_ctx.source_files[0] = hlsl_ctx.source_file;
    hlsl_ctx.source_files_count = 1;
    hlsl_ctx.cur_scope = NULL;
    hlsl_ctx.matrix_majority = HLSL_COLUMN_MAJOR;
    list_init(&hlsl_ctx.scopes);
    list_init(&hlsl_ctx.types);
    init_functions_tree(&hlsl_ctx.functions);
    list_init(&hlsl_ctx.static_initializers);
    list_init(&hlsl_ctx.extern_vars);

    push_scope(&hlsl_ctx);
    hlsl_ctx.globals = hlsl_ctx.cur_scope;
    declare_predefined_types(hlsl_ctx.globals);

    hlsl_parse();

    if (hlsl_ctx.status == PARSE_ERR)
        goto out;

    if (!(entry_func = get_func_entry(entrypoint)))
    {
        hlsl_message("error: entry point %s is not defined\n", debugstr_a(entrypoint));
        goto out;
    }

    if (!type_is_void(entry_func->return_type)
            && entry_func->return_type->type != HLSL_CLASS_STRUCT && !entry_func->return_var->semantic)
    {
        hlsl_report_message(entry_func->loc, HLSL_LEVEL_ERROR,
                "entry point \"%s\" is missing a return value semantic", entry_func->func->name);
    }

    list_move_head(entry_func->body, &hlsl_ctx.static_initializers);

    LIST_FOR_EACH_ENTRY(var, entry_func->parameters, struct hlsl_ir_var, param_entry)
    {
        if (var->modifiers & HLSL_STORAGE_UNIFORM)
            prepend_uniform_copy(entry_func->body, var);
        if (var->modifiers & HLSL_STORAGE_IN)
            prepend_input_var_copy(entry_func->body, var);
        if (var->modifiers & HLSL_STORAGE_OUT)
            append_output_var_copy(entry_func->body, var);
    }
    if (entry_func->return_var)
        append_output_var_copy(entry_func->body, entry_func->return_var);

    LIST_FOR_EACH_ENTRY(var, &hlsl_ctx.globals->vars, struct hlsl_ir_var, scope_entry)
    {
        if (var->modifiers & HLSL_STORAGE_UNIFORM)
            prepend_uniform_copy(entry_func->body, var);
    }

    transform_ir(fold_ident, entry_func->body, NULL);
    while (transform_ir(split_struct_copies, entry_func->body, NULL));
    while (transform_ir(fold_constants, entry_func->body, NULL));

    do
        compute_liveness(entry_func);
    while (transform_ir(dce, entry_func->body, NULL));

    compute_liveness(entry_func);

    if (TRACE_ON(hlsl_parser))
    {
        TRACE("IR dump.\n");
        wine_rb_for_each_entry(&hlsl_ctx.functions, dump_function, NULL);
    }

    allocate_temp_registers(entry_func);

    if (hlsl_ctx.status == PARSE_ERR)
        goto out;

    if (major < 4)
    {
        struct constant_defs constant_defs = allocate_const_registers(entry_func);
        hr = write_sm1_shader(entry_func, &constant_defs, shader_blob);
    }
    else
        hr = E_NOTIMPL;

out:
    if (messages)
    {
        if (hlsl_ctx.messages.size)
            *messages = hlsl_ctx.messages.string;
        else
            *messages = NULL;
    }
    else
    {
        if (hlsl_ctx.messages.capacity)
            d3dcompiler_free(hlsl_ctx.messages.string);
    }

    for (i = 0; i < hlsl_ctx.source_files_count; ++i)
        d3dcompiler_free((void *)hlsl_ctx.source_files[i]);
    d3dcompiler_free(hlsl_ctx.source_files);

    TRACE("Freeing functions IR.\n");
    wine_rb_destroy(&hlsl_ctx.functions, free_function_rb, NULL);

    TRACE("Freeing variables.\n");
    LIST_FOR_EACH_ENTRY_SAFE(scope, next_scope, &hlsl_ctx.scopes, struct hlsl_scope, entry)
    {
        LIST_FOR_EACH_ENTRY_SAFE(var, next_var, &scope->vars, struct hlsl_ir_var, scope_entry)
        {
            free_declaration(var);
        }
        wine_rb_destroy(&scope->types, NULL, NULL);
        d3dcompiler_free(scope);
    }

    TRACE("Freeing types.\n");
    LIST_FOR_EACH_ENTRY_SAFE(hlsl_type, next_type, &hlsl_ctx.types, struct hlsl_type, entry)
    {
        free_hlsl_type(hlsl_type);
    }

    return hr;
}
