
#ifndef __play_marshal_MARSHAL_H__
#define __play_marshal_MARSHAL_H__

#include	<glib-object.h>

G_BEGIN_DECLS

/* VOID:UINT,OBJECT,POINTER (./marshal.list:1) */
extern void play_marshal_VOID__UINT_OBJECT_POINTER (GClosure     *closure,
                                                    GValue       *return_value,
                                                    guint         n_param_values,
                                                    const GValue *param_values,
                                                    gpointer      invocation_hint,
                                                    gpointer      marshal_data);

/* VOID:UINT,STRING,POINTER (./marshal.list:2) */
extern void play_marshal_VOID__UINT_STRING_POINTER (GClosure     *closure,
                                                    GValue       *return_value,
                                                    guint         n_param_values,
                                                    const GValue *param_values,
                                                    gpointer      invocation_hint,
                                                    gpointer      marshal_data);

/* VOID:UINT,UINT64,UINT64,POINTER (./marshal.list:3) */
extern void play_marshal_VOID__UINT_UINT64_UINT64_POINTER (GClosure     *closure,
                                                           GValue       *return_value,
                                                           guint         n_param_values,
                                                           const GValue *param_values,
                                                           gpointer      invocation_hint,
                                                           gpointer      marshal_data);

/* VOID:ENUM,STRING (./marshal.list:4) */
extern void play_marshal_VOID__ENUM_STRING (GClosure     *closure,
                                            GValue       *return_value,
                                            guint         n_param_values,
                                            const GValue *param_values,
                                            gpointer      invocation_hint,
                                            gpointer      marshal_data);

/* VOID:STRING,STRING (./marshal.list:5) */
extern void play_marshal_VOID__STRING_STRING (GClosure     *closure,
                                              GValue       *return_value,
                                              guint         n_param_values,
                                              const GValue *param_values,
                                              gpointer      invocation_hint,
                                              gpointer      marshal_data);

G_END_DECLS

#endif /* __play_marshal_MARSHAL_H__ */

