''' copyright (c) 2010 Fredrik Kihlander, see LICENSE for more info '''

HEADER_TEMPLATE = '''/* Autogenerated file for DL-type-library! */

using System;
using System.Runtime.InteropServices;

%(structs)s

'''

CLASS_TEMPLATE = '''
// size32 %(size32)u, size64 %(size64)u, align32 %(align32)u, align64 %(align64)u
[StructLayout(LayoutKind.Sequential, Size=%(size32)u, CharSet=CharSet.Ansi)]
public class %(name)s
{
    public const UInt32 TYPE_ID = 0x%(typeid)08X;
    
    %(members)s
}'''

verbose = False # TODO: not global plox!!!

# TODO: read this config from user!!!
a_config_here = { 'int8'   : 'sbyte', 
                  'int16'  : 'short', 
                  'int32'  : 'int', 
                  'int64'  : 'long', 
                  'uint8'  : 'byte', 
                  'uint16' : 'ushort', 
                  'uint32' : 'uint', 
                  'uint64' : 'ulong', 
                  'fp32'   : 'float', 
                  'fp64'   : 'double', 
                  'string' : 'string' }

def to_csharp_name( typename ):
    if typename in a_config_here:
        return a_config_here[typename]
    return typename

def emit_member( member, mem_ctx ):
    import dl.typelibrary as tl
    
    lines = []
    if member.comment:
        lines.append( '// %s' % member.comment )
    
    cs_name = to_csharp_name( member.type )
    
    if   isinstance( member, tl.PodMember ):
        lines.append( 'public %s %s;' % ( cs_name, member.name ) )
    elif isinstance( member, tl.ArrayMember ):
        lines.append( 'public %s[] %s;' % ( cs_name, member.name ) ) 
        lines.append( 'private uint %sArraySize;' % member.name )
    elif isinstance( member, tl.InlineArrayMember ):
        lines.append( '[MarshalAsAttribute(UnmanagedType.ByVal%s, SizeConst=%u)]' % ( 'TStr' if member.type == 'string' else 'Array', member.count ) ) 
        lines.append( 'public %s[] %s;' % ( cs_name, member.name) )
    elif isinstance( member, tl.PointerMember ):
        lines.append( 'public %s %s = null;' % ( cs_name, member.name ) )
    elif isinstance( member, tl.BitfieldMember ):
        lines.extend( [ 'public %s %s' % ( member.type, member.name ),
                        '{',
                        '    get',
                        '    {',
                        '        %s MASK = (1 << %u) - 1;' % ( cs_name, member.bits ),
                        '        return (%s)((__BFStorage%d >> %u) & MASK);' % ( cs_name, mem_ctx.bf_group, member.offset.ptr32 ),
                        '    }',
                        '    set',
                        '    {',
                        '        %s MASK = (1 << %u) - 1;' % ( cs_name, member.bits ),
                        '        __BFStorage%d &= (%s)~(MASK << %u);' % ( mem_ctx.bf_group, cs_name, member.offset.ptr32 ),
                        '        __BFStorage%d |= (%s)((MASK & value) << %u);' % ( mem_ctx.bf_group, cs_name, member.offset.ptr32 ),
                        '    }',
                        '}' ] )
        if member.last_in_group:
            lines.append( 'private %s __BFStorage%d;' % ( cs_name, mem_ctx.bf_group ) )
            mem_ctx.bf_group += 1
    else:
        assert False, 'missing type (%s)!' % type( member )
    
    return lines

def emit_struct( type, stream ):
    if type.comment:
        print >> stream, '//', type.comment
        
    class MemberCreateCtx( object ):
        def __init__(self):
            self.bf_group = 0
        
    mem_ctx = MemberCreateCtx()
    
    member_lines = []
    for m in type.members:
        member_lines.extend( emit_member( m, mem_ctx ) )
    
    print >> stream, CLASS_TEMPLATE % { 'size32'  : type.size.ptr32,
                                        'size64'  : type.size.ptr64,
                                        'align32' : type.align.ptr32,
                                        'align64' : type.align.ptr64,
                                        'name'    : type.name,
                                        'typeid'  : type.typeid,
                                        'members' : '\n    '.join( member_lines ) }

def emit_enum( enum, stream ):
    stream.write( 'public enum %s : uint\n{' % enum.name )
    stream.write( ','.join( '\n\t%s = %u' % ( e.name, e.value ) for e in enum.values ) )
    stream.write( '\n};\n\n' )

def generate( typelibrary, config, stream ):
    from StringIO import StringIO
    
    structs = StringIO()
    
    for enum in typelibrary.enums.values():
        emit_enum( enum, structs )
        
    for type_name in typelibrary.type_order:
        emit_struct( typelibrary.types[type_name], structs )
        
    return HEADER_TEMPLATE % { 'structs' : structs.getvalue() }
