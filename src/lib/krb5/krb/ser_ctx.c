/*
 * lib/krb5/krb/ser_ctx.c
 *
 * Copyright 1995 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 */

/*
 * ser_ctx.c	- Routines to deal with serializing the krb5_context and
 *		  krb5_os_context structures.
 */
#include "k5-int.h"

/*
 * Routines to deal with externalizing the krb5_context:
 *	krb5_context_size();
 *	krb5_context_externalize();
 *	krb5_context_internalize();
 * 
 * Routines to deal with externalizing the krb5_os_context:
 *	krb5_oscontext_size();
 *	krb5_oscontext_externalize();
 *	krb5_oscontext_internalize();
 *
 * Routines to deal with externalizing the profile.
 *	profile_ser_size();
 *	profile_ser_externalize();
 *	profile_ser_internalize();
 *
 * Interface to initialize serializing of krb5_context and krb5_os_context:
 *	krb5_ser_context_init();
 */
static krb5_error_code krb5_context_size
	KRB5_PROTOTYPE((krb5_context, krb5_pointer, size_t *));
static krb5_error_code krb5_context_externalize
	KRB5_PROTOTYPE((krb5_context, krb5_pointer, krb5_octet **, size_t *));
static krb5_error_code krb5_context_internalize
	KRB5_PROTOTYPE((krb5_context,krb5_pointer *, krb5_octet **, size_t *));
static krb5_error_code krb5_oscontext_size
	KRB5_PROTOTYPE((krb5_context, krb5_pointer, size_t *));
static krb5_error_code krb5_oscontext_externalize
	KRB5_PROTOTYPE((krb5_context, krb5_pointer, krb5_octet **, size_t *));
static krb5_error_code krb5_oscontext_internalize
	KRB5_PROTOTYPE((krb5_context,krb5_pointer *, krb5_octet **, size_t *));
krb5_error_code profile_ser_size
	KRB5_PROTOTYPE((krb5_context, krb5_pointer, size_t *));
krb5_error_code profile_ser_externalize
	KRB5_PROTOTYPE((krb5_context, krb5_pointer, krb5_octet **, size_t *));
krb5_error_code profile_ser_internalize
	KRB5_PROTOTYPE((krb5_context,krb5_pointer *, krb5_octet **, size_t *));

/* Local data */
static const krb5_ser_entry krb5_context_ser_entry = {
    KV5M_CONTEXT,			/* Type 		*/
    krb5_context_size,			/* Sizer routine	*/
    krb5_context_externalize,		/* Externalize routine	*/
    krb5_context_internalize		/* Internalize routine	*/
};
static const krb5_ser_entry krb5_oscontext_ser_entry = {
    KV5M_OS_CONTEXT,			/* Type			*/
    krb5_oscontext_size,		/* Sizer routine	*/
    krb5_oscontext_externalize,		/* Externalize routine	*/
    krb5_oscontext_internalize		/* Internalize routine	*/
};
static const krb5_ser_entry krb5_profile_ser_entry = {
    PROF_MAGIC_PROFILE,			/* Type			*/
    profile_ser_size,			/* Sizer routine	*/
    profile_ser_externalize,		/* Externalize routine	*/
    profile_ser_internalize		/* Internalize routine	*/
};

/*
 * krb5_context_size()	- Determine the size required to externalize the
 *			  krb5_context.
 */
static krb5_error_code
krb5_context_size(kcontext, arg, sizep)
    krb5_context	kcontext;
    krb5_pointer	arg;
    size_t		*sizep;
{
    krb5_error_code	kret;
    size_t		required;
    krb5_context	context;

    /*
     * The KRB5 context itself requires:
     *	krb5_int32			for KV5M_CONTEXT
     *	krb5_int32			for sizeof(default_realm)
     *	strlen(default_realm)		for default_realm.
     *	krb5_int32			for netypes*sizeof(krb5_int32)
     *	netypes*sizeof(krb5_int32)	for etypes.
     *	krb5_int32			for trailer.
     */
    kret = EINVAL;
    if ((context = (krb5_context) arg)) {
	/* Calculate base length */
	required = (sizeof(krb5_int32) +
		    sizeof(krb5_int32) +
		    sizeof(krb5_int32) +
		    sizeof(krb5_int32) +
		    (context->etype_count * sizeof(krb5_int32)));

	if (context->default_realm)
	    required += strlen(context->default_realm);
	/* Calculate size required by os_context, if appropriate */
	if (context->os_context)
	    kret = krb5_size_opaque(kcontext,
				    KV5M_OS_CONTEXT,
				    (krb5_pointer) context->os_context,
				    &required);

	/* Calculate size required by db_context, if appropriate */
	if (!kret && context->db_context)
	    kret = krb5_size_opaque(kcontext,
				    KV5M_DB_CONTEXT,
				    (krb5_pointer) context->db_context,
				    &required);

	/* Finally, calculate size required by profile, if appropriate */
	if (!kret && context->profile)
	    kret = krb5_size_opaque(kcontext,
				    PROF_MAGIC_PROFILE,
				    (krb5_pointer) context->profile,
				    &required);
    }
    if (!kret)
	*sizep += required;
    return(kret);
}

/*
 * krb5_context_externalize()	- Externalize the krb5_context.
 */
static krb5_error_code
krb5_context_externalize(kcontext, arg, buffer, lenremain)
    krb5_context	kcontext;
    krb5_pointer	arg;
    krb5_octet		**buffer;
    size_t		*lenremain;
{
    krb5_error_code	kret;
    krb5_context	context;
    size_t		required;
    krb5_octet		*bp;
    size_t		remain;
    int			i;

    required = 0;
    bp = *buffer;
    remain = *lenremain;
    kret = EINVAL;
    if ((context = (krb5_context) arg)) {
	kret = ENOMEM;
	if (!krb5_context_size(kcontext, arg, &required) &&
	    (required <= remain)) {
	    /* First write our magic number */
	    (void) krb5_ser_pack_int32(KV5M_CONTEXT, &bp, &remain);
	    
	    /* Now sizeof default realm */
	    (void) krb5_ser_pack_int32((context->default_realm) ?
				       (krb5_int32) strlen(context->
							   default_realm) : 0,
				       &bp, &remain);

	    /* Now default_realm bytes */
	    if (context->default_realm)
		(void) krb5_ser_pack_bytes((krb5_octet *) 
					   context->default_realm,
					   strlen(context->default_realm),
					   &bp, &remain);

	    /* Now number of etypes */
	    (void) krb5_ser_pack_int32((krb5_int32) context->etype_count,
				       &bp, &remain);

	    /* Now serialize etypes */
	    for (i=0; i<context->etype_count; i++)
		(void) krb5_ser_pack_int32((krb5_int32) context->etypes[i],
					   &bp, &remain);
	    kret = 0;

	    /* Now handle os_context, if appropriate */
	    if (context->os_context)
		kret = krb5_externalize_opaque(kcontext,
					       KV5M_OS_CONTEXT,
					       (krb5_pointer)
					       context->os_context,
					       &bp,
					       &remain);

	    /* Now handle database context, if appropriate */
	    if (!kret && context->db_context)
		kret = krb5_externalize_opaque(kcontext,
					       KV5M_DB_CONTEXT,
					       (krb5_pointer)
					       context->db_context,
					       &bp,
					       &remain);

	    /* Finally, handle profile, if appropriate */
	    if (!kret && context->profile)
		kret = krb5_externalize_opaque(kcontext,
					       PROF_MAGIC_PROFILE,
					       (krb5_pointer)
					       context->profile,
					       &bp,
					       &remain);
	    /*
	     * If we were successful, write trailer then update the pointer and
	     * remaining length;
	     */
	    if (!kret) {
		/* Write our trailer */
		(void) krb5_ser_pack_int32(KV5M_CONTEXT, &bp, &remain);
		*buffer = bp;
		*lenremain = remain;
	    }
	}
    }
    return(kret);
}

/*
 * krb5_context_internalize()	- Internalize the krb5_context.
 */
static krb5_error_code
krb5_context_internalize(kcontext, argp, buffer, lenremain)
    krb5_context	kcontext;
    krb5_pointer	*argp;
    krb5_octet		**buffer;
    size_t		*lenremain;
{
    krb5_error_code	kret;
    krb5_context	context;
    krb5_int32		ibuf;
    krb5_octet		*bp;
    size_t		remain;
    int			i;

    bp = *buffer;
    remain = *lenremain;
    kret = EINVAL;
    /* Read our magic number */
    if (krb5_ser_unpack_int32(&ibuf, &bp, &remain))
	ibuf = 0;
    if (ibuf == KV5M_CONTEXT) {
	kret = ENOMEM;

	/* Get memory for the context */
	if (!(kret = krb5_ser_unpack_int32(&ibuf, &bp, &remain)) &&
	    (context = (krb5_context) malloc(sizeof(struct _krb5_context)))) {
	    memset(context, 0, sizeof(struct _krb5_context));

	    if (!ibuf ||
		(context->default_realm = (char *) malloc((size_t) ibuf+1))) {
		/* Copy in the default realm */
		if (ibuf) {
		    (void) krb5_ser_unpack_bytes((krb5_octet *)
						 context->default_realm,
						 (size_t) ibuf,
						 &bp, &remain);
		    context->default_realm[ibuf] = '\0';
		}

		/* Get the number of etypes */
		if (!(kret = krb5_ser_unpack_int32(&ibuf, &bp, &remain))) {
		    /* Reduce it to a count */
		    context->etype_count = ibuf;
		    if ((context->etypes = (krb5_enctype *)
			 malloc(sizeof(krb5_enctype) *
				(context->etype_count+1)))) {
			memset(context->etypes,
			       0,
			       sizeof(krb5_enctype) *
			       (context->etype_count + 1));
			for (i=0; i<context->etype_count; i++) {
			    if ((kret = krb5_ser_unpack_int32(&ibuf,
							      &bp, &remain)))
				break;
			    context->etypes[i] = (krb5_enctype) ibuf;
			}
		    }
		}

		/* Attempt to read in the os_context */
		if (!kret) {
		    kret = krb5_internalize_opaque(kcontext,
						   KV5M_OS_CONTEXT,
						   (krb5_pointer *)
						   &context->os_context,
						   &bp,
						   &remain);
		    if ((kret == EINVAL) || (kret == ENOENT))
			kret = 0;
		}

		/* Attempt to read in the db_context */
		if (!kret) {
		    kret = krb5_internalize_opaque(kcontext,
						   KV5M_DB_CONTEXT,
						   (krb5_pointer *)
						   &context->db_context,
						   &bp,
						   &remain);
		    if ((kret == EINVAL) || (kret == ENOENT))
			kret = 0;
		}

		/* Attempt to read in the profile */
		if (!kret) {
		    kret = krb5_internalize_opaque(kcontext,
						   PROF_MAGIC_PROFILE,
						   (krb5_pointer *)
						   &context->profile,
						   &bp,
						   &remain);
		    if ((kret == EINVAL) || (kret == ENOENT))
			kret = 0;
		}

		/* Finally, find the trailer */
		if (!kret) {
		    kret = krb5_ser_unpack_int32(&ibuf, &bp, &remain);
		    if (!kret && (ibuf == KV5M_CONTEXT))
			context->magic = KV5M_CONTEXT;
		    else
			kret = EINVAL;
		}
	    }
	    if (!kret) {
		*buffer = bp;
		*lenremain = remain;
		*argp = (krb5_pointer) context;
	    }
	    else
		krb5_free_context(context);
	}
    }
    return(kret);
}

/*
 * krb5_oscontext_size()	- Determine the size required to externalize
 *				  the krb5_os_context.
 */
static krb5_error_code
krb5_oscontext_size(kcontext, arg, sizep)
    krb5_context	kcontext;
    krb5_pointer	arg;
    size_t		*sizep;
{
    *sizep += sizeof(krb5_int32);
    return(0);
}

/*
 * krb5_oscontext_externalize()	- Externalize the krb5_os_context.
 */
static krb5_error_code
krb5_oscontext_externalize(kcontext, arg, buffer, lenremain)
    krb5_context	kcontext;
    krb5_pointer	arg;
    krb5_octet		**buffer;
    size_t		*lenremain;
{
    krb5_error_code	kret;
    krb5_os_context	os_ctx;
    size_t		required;
    krb5_octet		*bp;
    size_t		remain;

    required = 0;
    bp = *buffer;
    remain = *lenremain;
    kret = EINVAL;
    if ((os_ctx = (krb5_os_context) arg)) {
	kret = ENOMEM;
	if (!krb5_oscontext_size(kcontext, arg, &required) &&
	    (required <= remain)) {
	    (void) krb5_ser_pack_int32(KV5M_OS_CONTEXT, &bp, &remain);

	    /* Handle any other OS context here */
	    kret = 0;
	    if (!kret) {
		*buffer = bp;
		*lenremain = remain;
	    }
	}
    }
    return(kret);
}

/*
 * krb5_oscontext_internalize()	- Internalize the krb5_os_context.
 */
static krb5_error_code
krb5_oscontext_internalize(kcontext, argp, buffer, lenremain)
    krb5_context	kcontext;
    krb5_pointer	*argp;
    krb5_octet		**buffer;
    size_t		*lenremain;
{
    krb5_error_code	kret;
    krb5_os_context	os_ctx;
    krb5_int32		ibuf;
    krb5_octet		*bp;
    size_t		remain;

    bp = *buffer;
    remain = *lenremain;
    kret = EINVAL;
    /* Read our magic number */
    if (krb5_ser_unpack_int32(&ibuf, &bp, &remain))
	ibuf = 0;
    if (ibuf == KV5M_OS_CONTEXT) {
	kret = ENOMEM;

	/* Get memory for the context */
	if ((os_ctx = (krb5_os_context) 
	     malloc(sizeof(struct _krb5_os_context)))) {
	    memset(os_ctx, 0, sizeof(struct _krb5_os_context));
	    os_ctx->magic = KV5M_OS_CONTEXT;

	    /* Handle any more OS context here */

	    kret = 0;
	    *buffer = bp;
	    *lenremain = remain;
	}
    }
    if (!kret)
	*argp = (krb5_pointer) os_ctx;
    return(kret);
}

/*
 * Register the context serializers.
 */
krb5_error_code
krb5_ser_context_init(kcontext)
    krb5_context	kcontext;
{
    krb5_error_code	kret;
    kret = krb5_register_serializer(kcontext, &krb5_context_ser_entry);
    if (!kret)
	kret = krb5_register_serializer(kcontext, &krb5_oscontext_ser_entry);
    if (!kret)
	kret = krb5_register_serializer(kcontext, &krb5_profile_ser_entry);
    return(kret);
}
