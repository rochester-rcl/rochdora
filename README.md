# Rochdora

## Introduction

Customizations for Rochester including a batch preprocessor to create volumes.

## Requirements

This module requires the following modules/libraries:

* [Islandora](https://github.com/islandora/islandora)
* [Tuque](https://github.com/islandora/tuque)
* [Islandora Paged TEI Seadragon Viewer](https://github.com/discoverygarden/islandora_paged_tei_seadragon)
* [DiscoveryGarden's TEI Viewer](https://github.com/discoverygarden/tei_viewer)

## Installation

Install as usual, see [this](https://drupal.org/documentation/install/modules-themes/modules-7) for further information.

### Usage

The base ZIP/directory preprocessor can be called as a drush script (see drush help rochdora_prepare_diary for additional parameters):

drush -v --user=1 --uri=http://localhost rochdora_prepare_diary --type=zip --target=/path/to/archive.zip

This will populate the queue (stored in the Drupal database) with base entries.

The file structure used mirrors how the data was provided by the Rochester client data.
The structure required is laid out below:

> Phase1
>
>> a_volume
>>
>>> TIFFs
>>>
>>>> sometiff.tif
>
> Phase3
>
>> a_volume
>>
>>> sometiff_p3.tif
>>> sometiff_p3z.tif
>
> Phase5
>
>>final
>>
>>> a_volume
>>>
>>>> sometei.tei
>>
>> metadata
>>
>>> a_volume
>>>
>>>> somemods.mods

## Troubleshooting/Issues

Having problems or solved a problem? Contact [discoverygarden](http://support.discoverygarden.ca).

## Maintainers/Sponsors

Current maintainers:

* [discoverygarden](http://www.discoverygarden.ca)

## Development

If you would like to contribute to this module, please check out our helpful
[Documentation for Developers](https://github.com/Islandora/islandora/wiki#wiki-documentation-for-developers)
info, [Developers](http://islandora.ca/developers) section on Islandora.ca and
contact [discoverygarden](http://support.discoverygarden.ca).

## License

[GPLv3](http://www.gnu.org/licenses/gpl-3.0.txt)
