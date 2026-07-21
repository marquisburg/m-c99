/* Caller-owned MtlcType descriptors that mtlc/build.h has no constructor for.
 *
 * mtlc_type_scalar/mtlc_type_pointer only mint scalars and pointers; an array
 * type of N bytes has to be an MtlcType the frontend fills in and owns. Doing
 * that from Haskell would mean hardcoding the struct's field offsets, which
 * would break silently the next time mtlc/type.h changes. Here the C compiler
 * computes them.
 *
 * Descriptors are interned and immortal: codegen holds the pointer well past
 * the call that produced it, so they must never be freed.
 */
#include <mtlc/type.h>

#include <stdio.h>
#include <stdlib.h>

typedef struct BlobType {
  size_t bytes;
  MtlcType ty;
  char name[32];
  struct BlobType *next;
} BlobType;

static BlobType *blob_types;

typedef struct FnPtrType {
  MtlcType ty;
  const MtlcType *ret;
  MtlcType **params; /* owned array of borrowed descriptors */
  size_t count;
  char name[24];
  struct FnPtrType *next;
} FnPtrType;

static FnPtrType *fnptr_types;
static unsigned fnptr_serial;

/* A function-pointer descriptor carrying a real signature, so libmtlc can
 * classify an indirect call's arguments (integer vs XMM) and read a floating
 * return from XMM0 instead of RAX. Interned by (return, params); the name is
 * a serial ("fnp0", "fnp1", ...), unique enough for the module type registry
 * the DECLARE_LOCAL text resolves against. */
const MtlcType *c99m_fnptr_type(const MtlcType *ret,
                                const MtlcType *const *params, size_t count) {
  for (FnPtrType *f = fnptr_types; f; f = f->next) {
    if (f->ret != ret || f->count != count)
      continue;
    size_t i = 0;
    while (i < count && f->params[i] == params[i])
      i++;
    if (i == count)
      return &f->ty;
  }

  FnPtrType *f = (FnPtrType *)calloc(1, sizeof(FnPtrType));
  if (!f) {
    fprintf(stderr, "c99mtlc: out of memory\n");
    exit(1);
  }
  if (count > 0) {
    f->params = (MtlcType **)calloc(count, sizeof(MtlcType *));
    if (!f->params) {
      fprintf(stderr, "c99mtlc: out of memory\n");
      exit(1);
    }
    for (size_t i = 0; i < count; i++)
      f->params[i] = (MtlcType *)params[i];
  }
  f->ret = ret;
  f->count = count;
  snprintf(f->name, sizeof(f->name), "fnp%u", fnptr_serial++);
  f->ty.kind = MTLC_TYPE_FUNCTION_POINTER;
  f->ty.name = f->name;
  f->ty.size = 8;
  f->ty.alignment = 8;
  f->ty.fn_param_types = f->params;
  f->ty.fn_param_count = count;
  f->ty.fn_return_type = (MtlcType *)ret;
  f->next = fnptr_types;
  fnptr_types = f;
  return &f->ty;
}

/* An array-of-uint8 type of `bytes` (rounded up to 8), for stack storage of a
 * C aggregate via mtlc_local. */
const MtlcType *c99m_blob_type(size_t bytes) {
  if (bytes == 0)
    bytes = 1;
  size_t aligned = (bytes + 7u) & ~(size_t)7u;

  for (BlobType *b = blob_types; b; b = b->next)
    if (b->bytes == aligned)
      return &b->ty;

  BlobType *b = (BlobType *)calloc(1, sizeof(BlobType));
  if (!b) {
    fprintf(stderr, "c99mtlc: out of memory\n");
    exit(1);
  }
  b->bytes = aligned;
  snprintf(b->name, sizeof(b->name), "blob%zu", aligned);
  b->ty.kind = MTLC_TYPE_ARRAY;
  b->ty.name = b->name;
  b->ty.size = aligned;
  b->ty.alignment = 8;
  b->ty.base_type = (MtlcType *)mtlc_type_scalar(MTLC_TYPE_UINT8);
  b->ty.array_size = aligned;
  b->next = blob_types;
  blob_types = b;
  return &b->ty;
}
