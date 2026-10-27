# libdomainez - A C++ library

The `libdomainez` C++ library provides implementations for the REST APIs of
common european registrars: Gandi, Ionos, OVH. It specifically targets the
domain name management (availability, purchase, LiveDNS management).

## Usage

To start using `libdomainez` in your project, add the following `depends`
value to your `manifest`, adjusting the version constraint as appropriate:

```
depends: libdomainez ^1.0.0
```

Then import the library in your `buildfile`:

```
import libs = libdomainez%lib{domainez}
```


## Importable targets

This package provides the following importable targets:

```
lib{domainez}
```
