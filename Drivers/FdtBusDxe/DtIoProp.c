/** @file

    Copyright (c) 2023, Intel Corporation. All rights reserved.<BR>

    SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "FdtBusDxe.h"

/**
  Looks up property by name for a EFI_DT_IO_PROTOCOL instance.

  @param  This                  A pointer to the EFI_DT_IO_PROTOCOL instance.
  @param  Name                  Property to look up.
  @param  Property              Pointer to the EFI_DT_PROPERTY to fill.

  @retval EFI_SUCCESS           Lookup successful.
  @retval EFI_NOT_FOUND         Could not find property.
  @retval EFI_DEVICE_ERROR      Device Tree error.
  @retval EFI_INVALID_PARAMETER One or more parameters are invalid.

**/
EFI_STATUS
EFIAPI
DtIoGetProp (
  IN  EFI_DT_IO_PROTOCOL  *This,
  IN  CONST CHAR8         *Name,
  OUT EFI_DT_PROPERTY     *Property
  )
{
  DT_DEVICE   *DtDevice;
  CONST VOID  *Buf;
  INT32       Len;
  VOID        *TreeBase;

  if ((This == NULL) || (Property == NULL) || (Name == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  DtDevice = DT_DEV_FROM_THIS (This);
  TreeBase = GetTreeBaseFromDeviceFlags (DtDevice->Flags);
  Buf      = fdt_getprop (TreeBase, DtDevice->FdtNode, Name, &Len);
  if (Buf == NULL) {
    if (Len == -FDT_ERR_NOTFOUND) {
      return EFI_NOT_FOUND;
    }

    return EFI_DEVICE_ERROR;
  }

  Property->Begin = Buf;
  Property->Iter  = Buf;
  Property->End   = Buf + Len;

  return EFI_SUCCESS;
}

/**
  Parses out a U32 property value, advancing Prop->Iter on success.

  @param  This                  A pointer to the EFI_DT_IO_PROTOCOL instance.
  @param  Prop                  EFI_DT_PROPERTY describing the property buffer and
                                current position.
  @param  Type                  Type of the field to parse out.
  @param  Index                 Index of the field to return, starting from the
                                current buffer position within the EFI_DT_PROPERTY.
  @param  U32                   Pointer to a UINT32.
  @retval EFI_SUCCESS           Parsing successful.
  @retval EFI_NOT_FOUND         Not enough remaining property buffer to contain
                                the field of specified type.
  @retval EFI_INVALID_PARAMETER One or more parameters are invalid.

**/
STATIC
EFI_STATUS
EFIAPI
DtIoParsePropU32 (
  IN  DT_DEVICE            *DtDevice,
  IN  OUT EFI_DT_PROPERTY  *Prop,
  IN  UINTN                Index,
  OUT UINT32               *U32
  )
{
  UINTN              Cells;
  CONST EFI_DT_CELL  *Buf;

  Cells = (Prop->End - Prop->Iter) / sizeof (EFI_DT_CELL);
  Buf   = Prop->Iter;

  if (Cells <= Index) {
    return EFI_NOT_FOUND;
  }

  Buf       += Index;
  *U32       = fdt32_to_cpu (*Buf);
  Prop->Iter = Buf + 1;
  return EFI_SUCCESS;
}

/**
  Parses out a U64 property value, advancing Prop->Iter on success.

  @param  This                  A pointer to the EFI_DT_IO_PROTOCOL instance.
  @param  Prop                  EFI_DT_PROPERTY describing the property buffer and
                                current position.
  @param  Type                  Type of the field to parse out.
  @param  Index                 Index of the field to return, starting from the
                                current buffer position within the EFI_DT_PROPERTY.
  @param  U64                   Pointer to a UINT64.
  @retval EFI_SUCCESS           Parsing successful.
  @retval EFI_NOT_FOUND         Not enough remaining property buffer to contain
                                the field of specified type.
  @retval EFI_INVALID_PARAMETER One or more parameters are invalid.

**/
STATIC
EFI_STATUS
EFIAPI
DtIoParsePropU64 (
  IN  DT_DEVICE            *DtDevice,
  IN  OUT EFI_DT_PROPERTY  *Prop,
  IN  UINTN                Index,
  OUT UINT64               *U64
  )
{
  UINTN              ElemCells;
  UINT8              Iter;
  UINTN              Cells;
  CONST EFI_DT_CELL  *Buf;

  ElemCells = 2;
  Cells     = (Prop->End - Prop->Iter) / sizeof (EFI_DT_CELL);
  Buf       = Prop->Iter;

  if ((Cells / ElemCells) <= Index) {
    return EFI_NOT_FOUND;
  }

  Buf += ElemCells * Index;

  *U64 = 0;
  for (Iter = 0; Iter < ElemCells; Iter++, Buf++) {
    *U64 |= ((UINT64)fdt32_to_cpu (*Buf)) <<
            (32 * (ElemCells - (Iter + 1)));
  }

  Prop->Iter = Buf;
  return EFI_SUCCESS;
}

/**
  Parses out a bus address property value, advancing Prop->Iter on success.

  @param  This                  A pointer to the EFI_DT_IO_PROTOCOL instance.
  @param  Prop                  EFI_DT_PROPERTY describing the property buffer and
                                current position.
  @param  Type                  Type of the field to parse out.
  @param  Index                 Index of the field to return, starting from the
                                current buffer position within the EFI_DT_PROPERTY.
  @param  BusAddress            Pointer to EFI_DT_BUS_ADDRESS.
  @retval EFI_SUCCESS           Parsing successful.
  @retval EFI_NOT_FOUND         Not enough remaining property buffer to contain
                                the field of specified type.
  @retval EFI_INVALID_PARAMETER One or more parameters are invalid.

**/
STATIC
EFI_STATUS
EFIAPI
DtIoParsePropBusAddress (
  IN  DT_DEVICE            *DtDevice,
  IN  OUT EFI_DT_PROPERTY  *Prop,
  IN  UINTN                Index,
  OUT EFI_DT_BUS_ADDRESS   *BusAddress
  )
{
  UINTN              ElemCells;
  UINT8              Iter;
  UINTN              Cells;
  CONST EFI_DT_CELL  *Buf;
  UINT8              AddressCells;

  AddressCells = DtDevice->DtIo.AddressCells;

  //
  // Enforced in FdtGetAddressCells.
  //

  ASSERT (AddressCells <= FDT_MAX_NCELLS);

  ElemCells = AddressCells;
  Cells     = (Prop->End - Prop->Iter) / sizeof (EFI_DT_CELL);
  Buf       = Prop->Iter;

  if ((Cells / ElemCells) <= Index) {
    return EFI_NOT_FOUND;
  }

  Buf += ElemCells * Index;

  *BusAddress = 0;
  for (Iter = 0; Iter < ElemCells; Iter++, Buf++) {
    *BusAddress |= ((EFI_DT_BUS_ADDRESS)fdt32_to_cpu (*Buf)) <<
                   (32 * (ElemCells - (Iter + 1)));
  }

  Prop->Iter = Buf;
  return EFI_SUCCESS;
}

/**
  Parses out a size property value, advancing Prop->Iter on success.

  @param  This                  A pointer to the EFI_DT_IO_PROTOCOL instance.
  @param  Prop                  EFI_DT_PROPERTY describing the property buffer and
                                current position.
  @param  Type                  Type of the field to parse out.
  @param  Index                 Index of the field to return, starting from the
                                current buffer position within the EFI_DT_PROPERTY.
  @param  Size                  Pointer to EFI_DT_SIZE.
  @retval EFI_SUCCESS           Parsing successful.
  @retval EFI_NOT_FOUND         Not enough remaining property buffer to contain
                                the field of specified type.
  @retval EFI_INVALID_PARAMETER One or more parameters are invalid.

**/
STATIC
EFI_STATUS
EFIAPI
DtIoParsePropSize (
  IN  DT_DEVICE            *DtDevice,
  IN  OUT EFI_DT_PROPERTY  *Prop,
  IN  UINTN                Index,
  OUT EFI_DT_SIZE          *Size
  )
{
  UINTN              ElemCells;
  UINT8              Iter;
  UINTN              Cells;
  CONST EFI_DT_CELL  *Buf;
  UINT8              SizeCells;

  SizeCells = DtDevice->DtIo.SizeCells;

  //
  // Enforced in FdtGetSizeCells.
  //

  ASSERT (SizeCells <= FDT_MAX_NCELLS);

  ElemCells = SizeCells;
  Cells     = (Prop->End - Prop->Iter) / sizeof (EFI_DT_CELL);
  Buf       = Prop->Iter;

  if ((Cells / ElemCells) <= Index) {
    return EFI_NOT_FOUND;
  }

  Buf += ElemCells * Index;

  *Size = 0;
  for (Iter = 0; Iter < ElemCells; Iter++, Buf++) {
    *Size |= ((EFI_DT_SIZE)fdt32_to_cpu (*Buf)) <<
             (32 * (ElemCells - (Iter + 1)));
  }

  Prop->Iter = Buf;
  return EFI_SUCCESS;
}

/**
  Parses out reg property value, advancing Prop->Iter on success.

  @param  This                  A pointer to the EFI_DT_IO_PROTOCOL instance.
  @param  Prop                  EFI_DT_PROPERTY describing the property buffer and
                                current position.
  @param  Type                  Type of the field to parse out.
  @param  Index                 Index of the field to return, starting from the
                                current buffer position within the EFI_DT_PROPERTY.
  @param  Reg                   Pointer to EFI_DT_REG.
  @retval EFI_SUCCESS           Parsing successful.
  @retval EFI_NOT_FOUND         Not enough remaining property buffer to contain
                                the field of specified type.
  @retval EFI_INVALID_PARAMETER One or more parameters are invalid.

**/
STATIC
EFI_STATUS
EFIAPI
DtIoParsePropReg (
  IN  DT_DEVICE            *DtDevice,
  IN  OUT EFI_DT_PROPERTY  *Prop,
  IN  UINTN                Index,
  OUT EFI_DT_REG           *Reg
  )
{
  UINTN       ElemCells;
  UINTN       Cells;
  EFI_STATUS  Status;
  DT_DEVICE   *BusDevice;
  UINT8       AddressCells;
  UINT8       SizeCells;
  CONST VOID  *OriginalIter;

  AddressCells = DtDevice->DtIo.AddressCells;
  SizeCells    = DtDevice->DtIo.SizeCells;

  //
  // Enforced in FdtGetAddressCells/FdtGetSizeCells.
  //

  ASSERT (AddressCells <= FDT_MAX_NCELLS);
  ASSERT (SizeCells <= FDT_MAX_NCELLS);

  ElemCells    = AddressCells + SizeCells;
  Cells        = (Prop->End - Prop->Iter) / sizeof (EFI_DT_CELL);
  OriginalIter = Prop->Iter;

  if ((Cells / ElemCells) <= Index) {
    return EFI_NOT_FOUND;
  }

  Prop->Iter = (EFI_DT_CELL *)Prop->Iter + ElemCells * Index;

  Status = DtIoParsePropBusAddress (DtDevice, Prop, 0, &Reg->Base);
  if (EFI_ERROR (Status)) {
    goto Out;
  }

  Status = DtIoParsePropSize (DtDevice, Prop, 0, &Reg->Length);
  if (EFI_ERROR (Status)) {
    goto Out;
  }

  Reg->BusDtIo = NULL;
  BusDevice    = NULL;
  Status       = DtDeviceTranslateRangeToCpu (
                   DtDevice,
                   &Reg->Base,
                   &Reg->Length,
                   &Reg->Base,
                   &BusDevice
                   );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: DtDeviceTranslateRangeToCpu: %r\n",
      __func__,
      Status
      ));
    goto Out;
  }

  if (BusDevice != NULL) {
    Reg->BusDtIo = &BusDevice->DtIo;
  }

Out:
  if (EFI_ERROR (Status)) {
    Prop->Iter = OriginalIter;
  }

  return Status;
}

/**
  Parses out a field encoded in the property, advancing Prop->Iter on success.

  @param  This                  A pointer to the EFI_DT_IO_PROTOCOL instance.
  @param  Prop                  EFI_DT_PROPERTY describing the property buffer and
                                current position.
  @param  Type                  Type of the field to parse out.
  @param  Index                 Index of the field to return, starting from the
                                current buffer position within the EFI_DT_PROPERTY.
  @param  Buffer                Pointer to a buffer large enough to contain the
                                parsed out field.
  @retval EFI_SUCCESS           Parsing successful.
  @retval EFI_NOT_FOUND         Not enough remaining property buffer to contain
                                the field of specified type.
  @retval EFI_INVALID_PARAMETER One or more parameters are invalid.

**/
EFI_STATUS
EFIAPI
DtIoParseProp (
  IN  EFI_DT_IO_PROTOCOL   *This,
  IN  OUT EFI_DT_PROPERTY  *Prop,
  IN  EFI_DT_VALUE_TYPE    Type,
  IN  UINTN                Index,
  OUT VOID                 *Buffer
  )
{
  DT_DEVICE  *DtDevice;

  if ((This == NULL) || (Prop == NULL) || (Buffer == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  DtDevice = DT_DEV_FROM_THIS (This);

  switch (Type) {
    case EFI_DT_VALUE_U32:
      return DtIoParsePropU32 (DtDevice, Prop, Index, Buffer);
    case EFI_DT_VALUE_U64:
      return DtIoParsePropU64 (DtDevice, Prop, Index, Buffer);
    case EFI_DT_VALUE_BUS_ADDRESS:
      return DtIoParsePropBusAddress (DtDevice, Prop, Index, Buffer);
    case EFI_DT_VALUE_SIZE:
      return DtIoParsePropSize (DtDevice, Prop, Index, Buffer);
    case EFI_DT_VALUE_REG:
      return DtIoParsePropReg (DtDevice, Prop, Index, Buffer);
    case EFI_DT_VALUE_RANGE:
    case EFI_DT_VALUE_STRING:
    case EFI_DT_VALUE_LOOKUP:
      break;
  }

  return EFI_UNSUPPORTED;
}

/**
  Looks up a reg property value by name for a EFI_DT_IO_PROTOCOL instance.

  @param  This                  A pointer to the EFI_DT_IO_PROTOCOL instance.
  @param  Index                 Index of the reg value to return.
  @param  Reg                   Pointer to the EFI_DT_REG to fill.

  @retval EFI_SUCCESS           Lookup successful.
  @retval EFI_NOT_FOUND         Could not find property.
  @retval EFI_DEVICE_ERROR      Device Tree error.
  @retval EFI_INVALID_PARAMETER One or more parameters are invalid.

**/
EFI_STATUS
EFIAPI
DtIoGetReg (
  IN  EFI_DT_IO_PROTOCOL  *This,
  IN  UINTN               Index,
  OUT EFI_DT_REG          *Reg
  )
{
  EFI_STATUS       Status;
  EFI_DT_PROPERTY  Property;
  DT_DEVICE        *DtDevice;

  if ((This == NULL) || (Reg == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  DtDevice = DT_DEV_FROM_THIS (This);

  Status = DtIoGetProp (This, "reg", &Property);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = DtIoParseProp (
             This,
             &Property,
             EFI_DT_VALUE_REG,
             Index,
             Reg
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EFI_SUCCESS;
}
