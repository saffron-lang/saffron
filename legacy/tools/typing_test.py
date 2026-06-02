import abc
import dataclasses
import typing


class Type(abc.ABC):
    pass


@dataclasses.dataclass
class BuiltinType(Type):
    """Class type with fields etc"""
    name: str
    super: Type | None = None


@dataclasses.dataclass
class NameType(Type):
    """A named reference to a type in scope"""
    name: str


@dataclasses.dataclass
class GenericDef(Type):
    """A definition of a generic to be referenced"""
    name: str
    extends: Type = None


@dataclasses.dataclass
class InterfaceType(Type):
    """Matches anything with the same typed fields"""
    generics: list[GenericDef]
    body: dict[str, Type]


@dataclasses.dataclass
class FunctionType(Type):
    generics: list[GenericDef]
    args: dict[str, Type]
    rtype: Type


@dataclasses.dataclass
class GenericType(Type):
    base: Type
    generic_args: list[Type]


@dataclasses.dataclass
class Environment:
    enclosing: typing.Optional['Environment']
    vars: dict[str, Type]
    types: dict[str, Type]


@dataclasses.dataclass
class TypeContext(Type):
    inner: Type
    resolutions: dict[str, Type]


global_types = dict(
    Iterator=InterfaceType(generics=[GenericDef('T')], body=dict(
        next=FunctionType(generics=[], args=dict(), rtype=NameType('T')),
        nextQ=FunctionType(generics=[], args=dict(), rtype=NameType('Bool')),
        iter=FunctionType(
            generics=[],
            args=dict(),
            rtype=GenericType(
                base=NameType('Iterator'),
                generic_args=[NameType('T')],
            )
        ),
    )),
    Number=BuiltinType('Number'),
    Bool=BuiltinType('Bool'),
    Float=BuiltinType('Float', NameType('Number')),
    Int=BuiltinType('Int', NameType('Number')),
)

global_vars = dict(
    filter=FunctionType(
        generics=[GenericDef('K')],
        args=dict(
            iter=GenericType(base=NameType('Iterator'), generic_args=[NameType('K')]),
            f=FunctionType(
                generics=[],
                args=dict(value=NameType('K')),
                rtype=NameType('Bool'),
            )
        ),
        rtype=GenericType(base=NameType('Iterator'), generic_args=[NameType('K')])
    ),
)

global_environment = Environment(None, global_vars, global_types)


def resolve_var(env: Environment | None, name: str) -> Type | None:
    if env is None:
        raise RuntimeError("Undefined variable %s" % name)

    type = env.vars.get(name)
    if type is None:
        return resolve_var(env.enclosing, name)

    return type


def resolve_type(env: Environment | None, name: str) -> Type | None:
    if env is None:
        raise RuntimeError("Undefined type %s" % name)

    type = env.types.get(name)
    if type is None:
        return resolve_type(env.enclosing, name)

    return type


def evaluate_call(env: Environment, type: Type):
    if isinstance(type, TypeContext):
        inner_env = Environment(env, dict(), type.resolutions)
        result = evaluate_call(inner_env, type.inner)
        if isinstance(result, BuiltinType):
            return result
        return TypeContext(result, type.resolutions)

    if isinstance(type, GenericType):
        resolutions = dict()
        basetype = resolve_type(env, type.base)
        if isinstance(basetype, (BuiltinType,)):
            return basetype

        for generic, arg in zip(basetype.generics, type.generic_args):
            resolutions[generic.name] = resolve_names(env, arg)
        return TypeContext(basetype.rtype, resolutions)
    else:
        return type.rtype


def evaluate_attribute(env: Environment, type: Type, name: str):
    if isinstance(type, TypeContext):
        inner_env = Environment(env, dict(), type.resolutions)
        result = evaluate_attribute(inner_env, type.inner, name)
        if isinstance(result, BuiltinType):
            return result
        return TypeContext(result, type.resolutions)

    if isinstance(type, GenericType):
        resolutions = dict()
        basetype = resolve_names(env, type.base)

        if isinstance(basetype, (BuiltinType,)):
            return basetype

        for generic, arg in zip(basetype.generics, type.generic_args):
            resolutions[generic.name] = resolve_names(env, arg)
        return TypeContext(basetype.body[name], resolutions)
    else:
        return type.body[name]


def resolve_names(env: Environment, type: Type):
    if isinstance(type, GenericDef):
        return resolve_type(env, type.name)
    elif isinstance(type, NameType):
        return resolve_type(env, type.name)
    else:
        return type


def resolve_contexts(env: Environment, type: Type) -> Type:
    if isinstance(type, GenericDef):
        return resolve_type(env, type.name)
    elif isinstance(type, NameType):
        while isinstance(type, NameType):
            type = resolve_type(env, type.name)
        return type
    elif isinstance(type, FunctionType):
        inner_env = Environment(env, dict(), dict())
        for generic in type.generics:
            inner_env.types[generic.name] = generic

        return FunctionType(
            generics=type.generics,
            args={name: resolve_contexts(inner_env, argtype) for name, argtype in type.args.items()},
            rtype=resolve_contexts(inner_env, type.rtype),
        )
    elif isinstance(type, InterfaceType):
        inner_env = Environment(env, dict(), dict())
        for generic in type.generics:
            inner_env.types[generic.name] = generic

        return InterfaceType(
            generics=type.generics,
            body={name: resolve_contexts(inner_env, argtype) for name, argtype in type.body.items()},
        )
    elif isinstance(type, GenericType):
        # Iterator<Number>
        basetype = resolve_contexts(env, type.base)
        return GenericType(basetype, [resolve_contexts(env, t) for t in type.generic_args])
    elif isinstance(type, BuiltinType):
        return type
    elif isinstance(type, TypeContext):
        return resolve_contexts(Environment(env, dict(), type.resolutions), type.inner)


test_env = Environment(
    global_environment,
    dict(
        a=GenericType(
            base=NameType('Iterator'),
            generic_args=[NameType('Number')],
        )
    ),
    dict()
)

call = evaluate_call(test_env, evaluate_attribute(test_env, resolve_var(test_env, 'a'), 'iter'))


# pprint(call)
# pprint(resolve_contexts(test_env, call))


def issubtype(env: Environment, subtype: Type, supertype: Type):
    print("Sub:", subtype)
    print("Sup:", supertype)
    if isinstance(subtype, TypeContext):
        return issubtype(Environment(env, dict(), subtype.resolutions), subtype.inner, supertype)
    if isinstance(supertype, TypeContext):
        return issubtype(Environment(env, dict(), supertype.resolutions), subtype, supertype.inner)

    if isinstance(subtype, NameType):
        return issubtype(env, resolve_names(env, subtype), supertype)
    if isinstance(supertype, NameType):
        return issubtype(env, subtype, resolve_names(env, supertype))

    if isinstance(subtype, BuiltinType) and isinstance(supertype, BuiltinType):
        if subtype.super:
            return issubtype(env, subtype.super, supertype)

    if isinstance(subtype, GenericType) and isinstance(supertype, GenericType):
        # Require all generic args to match and parent type
        # to be subtype
        for superarg, subarg in zip(supertype.generic_args, subtype.generic_args):
            if not issametype(env, subarg, superarg):
                return False

        return issubtype(env, subtype.base, supertype.base)

    # pprint(resolved_sub)
    # pprint(resolved_sup)
    # Could account for Any or Never here
    if subtype is supertype:
        return True

    if subtype == supertype:
        print("THIS SHOULD NEVER HAPPEN!")
        return True

    return False


def issametype(env: Environment, left: Type, right: Type):
    print("Left:", left)
    print("Right:", right)
    if isinstance(left, TypeContext):
        return issametype(Environment(env, dict(), left.resolutions), left.inner, right)
    if isinstance(right, TypeContext):
        return issametype(Environment(env, dict(), right.resolutions), left, right.inner)

    if isinstance(left, NameType):
        return issametype(env, resolve_names(env, left), right)
    if isinstance(right, NameType):
        return issametype(env, left, resolve_names(env, right))

    if isinstance(left, BuiltinType) and isinstance(right, BuiltinType):
        if left.super:
            return issametype(env, left.super, right)

    if isinstance(left, GenericType) and isinstance(right, GenericType):
        # Require all generic args to match and parent type
        # to be subtype
        for superarg, subarg in zip(right.generic_args, left.generic_args):
            if not issametype(env, subarg, superarg):
                return False

        return issametype(env, left.base, right.base)

    resolved_sub = resolve_contexts(env, left)
    resolved_sup = resolve_contexts(env, right)

    # pprint(resolved_sub)
    # pprint(resolved_sup)
    # Could account for Any or Never here
    if resolved_sub is resolved_sup:
        return True

    if resolved_sub == resolved_sup:
        print("THIS SHOULD NEVER HAPPEN!")
        return True

    return False


print(issubtype(
    test_env,
    evaluate_call(
        test_env,
        evaluate_attribute(test_env, resolve_var(test_env, 'a'), 'iter')
    ),
    GenericType(NameType('Iterator'), [NameType('Number')])
))

print(issubtype(
    test_env,
    evaluate_call(
        test_env,
        evaluate_attribute(test_env, resolve_var(test_env, 'a'), 'next')
    ),
    NameType('Number')
))

print("Int is Number:", issubtype(test_env, NameType('Int'), NameType('Number')))
