/*
 * Copyright (C) 2011-2016 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */


#include "se_wrapper.h"
#include "se_error_internal.h"
#include "arch.h"
#include "util.h"
#include "loader.h"
#include "se_page_attr.h"
#include "enclave.h"
#include "enclave_creator.h"
#include "routine.h"
#include "sgx_attributes.h"
#include "se_vendor.h"
#include "se_detect.h"
#include "binparser.h"
#include <assert.h>
#include <vector>
#include <algorithm>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <sys/mman.h>

// enclave creator instance
extern EnclaveCreator* g_enclave_creator;

EnclaveCreator* get_enclave_creator(void)
{
    return g_enclave_creator;
}

CLoader::CLoader(uint8_t *mapped_file_base, BinParser &parser)
    : m_mapped_file_base(mapped_file_base)
    , m_enclave_id(0)
    , m_start_addr(NULL)
    , m_metadata(NULL)
    , m_parser(parser)
{
    memset(&m_secs, 0, sizeof(m_secs));
}

CLoader::~CLoader()
{

}

sgx_enclave_id_t CLoader::get_enclave_id() const
{
    return m_enclave_id;
}

const void* CLoader::get_start_addr() const
{
    return m_start_addr;
}

const std::vector<tcs_t *>& CLoader::get_tcs_list() const
{
    return m_tcs_list;
}

const secs_t& CLoader::get_secs() const
{
    return m_secs;
}

void* CLoader::get_symbol_address(const char * const symbol)
{
    uint64_t rva = m_parser.get_symbol_rva(symbol);
    if(0 == rva)
        return NULL;
    return GET_PTR(void, m_start_addr, rva);
}

int CLoader::build_mem_region(const section_info_t * const sec_info)
{
    int ret = SGX_SUCCESS;
    uint8_t added_page[SE_PAGE_SIZE];
    uint64_t offset = 0;
    uint8_t *raw_ptr = NULL;
    uint64_t rva = 0;
    sec_info_t sinfo;
    memset(&sinfo, 0, sizeof(sinfo));

    rva = sec_info->rva + offset;
    while(offset < TRIM_TO_PAGE(sec_info->raw_data_size))
    {
        raw_ptr = sec_info->raw_data + offset;
        sinfo.flags = sec_info->flag;

        //check if the page is writable.
        if(sec_info->bitmap && sec_info->bitmap->size())
        {
            uint64_t page_frame = rva >> SE_PAGE_SHIFT;

            //NOTE:
            //  Current enclave size is not beyond 64G, so the type-casting from (uint64>>15) to (size_t) is OK.
            //  In the future, if the max enclave size is extended to beyond (1<<49), this type-casting will not work.
            //  It only impacts the enclave signing process. (32bit signing tool to sign 64 bit enclaves)
            if((*sec_info->bitmap)[(size_t)(page_frame / 8)] & (1 << (page_frame % 8)))
                sinfo.flags = sec_info->flag | SI_FLAG_W;
        }
        //call driver API to add page; raw_ptr needn't be page align, driver will handle page align;
        if(SGX_SUCCESS != (ret = get_enclave_creator()->add_enclave_page(ENCLAVE_ID_IOCTL, raw_ptr, rva, sinfo, ADD_EXTEND_PAGE)))
        {
            //if add page failed , we should remove enclave somewhere;
            return ret;
        }
        offset += SE_PAGE_SIZE;
        rva = sec_info->rva + offset;
    }
    //add the remaider of last page of raw data
    if(!IS_PAGE_ALIGNED(sec_info->raw_data_size))
    {
        sinfo.flags = sec_info->flag;
        //the padding be 0
        memset(added_page, 0, SE_PAGE_SIZE);
        raw_ptr = sec_info->raw_data + offset;
        rva = sec_info->rva + offset;
        memcpy_s(added_page, SE_PAGE_SIZE, raw_ptr, sec_info->raw_data_size & (SE_PAGE_SIZE-1));
        //check if the page is writable.
        if(sec_info->bitmap && sec_info->bitmap->size())
        {
            uint64_t page_frame = rva >> SE_PAGE_SHIFT;
            //NOTE:
            //  Current enclave size is not beyond 64G, so the type-casting from (uint64>>15) to (size_t) is OK.
            //  In the future, if the max enclave size is extended to beyond (1<<49), this type-casting will not work.
            //  It only impacts the enclave signing process. (32bit signing tool to sign 64 bit enclaves)
            if((*sec_info->bitmap)[(size_t)(page_frame / 8)] & (1 << (page_frame % 8)))
                sinfo.flags = sec_info->flag | SI_FLAG_W;
        }
        //call driver to add page;
        if(SGX_SUCCESS != (ret = get_enclave_creator()->add_enclave_page(ENCLAVE_ID_IOCTL, added_page, rva, sinfo, ADD_EXTEND_PAGE)))
        {
            //if add page failed , we should remove enclave somewhere;
            return ret;
        }
        rva += SE_PAGE_SIZE;
    }
    //add unintialized page.If the section have no raw data, the offset should be 0.
    if(ROUND_TO_PAGE(sec_info->virtual_size) > ROUND_TO_PAGE(sec_info->raw_data_size))
    {
        size_t size = (size_t)(ROUND_TO_PAGE(sec_info->virtual_size) - ROUND_TO_PAGE(sec_info->raw_data_size));
        sinfo.flags = sec_info->flag;
        if(SGX_SUCCESS != (ret = build_pages(rva, size, 0, sinfo, ADD_EXTEND_PAGE)))
            return ret;
    }

    return SGX_SUCCESS;
}

int CLoader::build_sections(vector<uint8_t> *bitmap)
{
    int ret = SGX_SUCCESS;
    std::vector<Section*> sections = m_parser.get_sections();

    for(unsigned int i = 0; i < sections.size() ; i++)
    {
        //since build_mem_region require the sec_info.rva be page aligned, we need handle the first page.
        //build the first page;
        uint64_t offset = (sections[i]->get_rva() & (SE_PAGE_SIZE -1));
        uint64_t size = SE_PAGE_SIZE - offset;
        uint8_t first_page[SE_PAGE_SIZE];
        //the raw data may be smaller than the size, we get the min of them
        if(sections[i]->raw_data_size() < size)
            size = sections[i]->raw_data_size();
        //the padding is '0'
        memset(first_page, 0, SE_PAGE_SIZE);
        memcpy_s(&first_page[offset], (size_t)size, sections[i]->raw_data(), (size_t)size);
        section_info_t sec_info = { first_page, SE_PAGE_SIZE, sections[i]->get_rva() & (~(SE_PAGE_SIZE - 1)), SE_PAGE_SIZE, sections[i]->get_si_flags(), bitmap };

        if(SGX_SUCCESS != (ret = build_mem_region(&sec_info)))
        {
            return ret;
        }
        //if there is more pages, then build the next paged aligned pages
        if((sections[i]->virtual_size() + offset) >  SE_PAGE_SIZE)
        {
            sec_info.raw_data = GET_PTR(uint8_t, sections[i]->raw_data(), size);
            sec_info.raw_data_size = sections[i]->raw_data_size() - size;
            sec_info.rva = sections[i]->get_rva() + (SE_PAGE_SIZE - offset);
            assert(0 == (sec_info.rva & (SE_PAGE_SIZE - 1)));
            //we need use (SE_PAGE_SIZE - offset), because (SE_PAGE_SIZE - offset) may larger than size
            sec_info.virtual_size = sections[i]->virtual_size() - (SE_PAGE_SIZE - offset);
            sec_info.flag = sections[i]->get_si_flags();
            sec_info.bitmap = bitmap;
            if(SGX_SUCCESS != (ret = build_mem_region(&sec_info)))
            {
                return ret;
            }
        }
    }

    return SGX_SUCCESS;
}

int CLoader::build_pages(const uint64_t start_rva, const uint64_t size, void *source, const sec_info_t &sinfo, const uint32_t attr)
{
    int ret = SGX_SUCCESS;
    uint64_t offset = 0;
    uint64_t rva = start_rva;

    while(offset < size)
    {
        //call driver to add page;
        if(SGX_SUCCESS != (ret = get_enclave_creator()->add_enclave_page(ENCLAVE_ID_IOCTL, source, rva, sinfo, attr)))
        {
            //if add page failed , we should remove enclave somewhere;
            return ret;
        }
        offset += SE_PAGE_SIZE;
        rva += SE_PAGE_SIZE;
    }

    return SGX_SUCCESS;
}
int CLoader::build_context(const uint64_t start_rva, layout_entry_t *layout)
{
    int ret = SGX_ERROR_UNEXPECTED;
    uint8_t added_page[SE_PAGE_SIZE];
    sec_info_t sinfo;
    memset(&sinfo, 0, sizeof(sinfo));
    uint64_t rva = start_rva + layout->rva;

    if (layout->content_offset)
    {
        // assume TCS is only 1 page
        if(layout->si_flags == SI_FLAGS_TCS)
        {
            memset(added_page, 0, SE_PAGE_SIZE);
            memcpy_s(added_page, SE_PAGE_SIZE, GET_PTR(uint8_t, m_metadata, layout->content_offset), layout->content_size);

            tcs_t *ptcs = reinterpret_cast<tcs_t*>(added_page);
            ptcs->ossa += rva;
            ptcs->ofs_base += rva;
            ptcs->ogs_base += rva;
            m_tcs_list.push_back(GET_PTR(tcs_t, m_start_addr, rva));
            sinfo.flags = layout->si_flags;
            if(SGX_SUCCESS != (ret = build_pages(rva, layout->page_count << SE_PAGE_SHIFT, added_page, sinfo, layout->attributes)))
            {
                return ret;
            }
        }
        else // guard page should not have content_offset != 0 
        {
            section_info_t sec_info = {GET_PTR(uint8_t, m_metadata, layout->content_offset), layout->content_size, rva, layout->page_count << SE_PAGE_SHIFT, layout->si_flags, NULL};
            if(SGX_SUCCESS != (ret = build_mem_region(&sec_info)))
            {
                return ret;
            }
        }
    }
    else if (layout->si_flags != SI_FLAG_NONE)
    {
        sinfo.flags = layout->si_flags;

        void *source = NULL;
        if(layout->content_size)
        {
            for(uint32_t *p = (uint32_t *)added_page; p < GET_PTR(uint32_t, added_page, SE_PAGE_SIZE); p++)
            {
                *p = layout->content_size;
            }
            source = added_page;
        }
        if(SGX_SUCCESS != (ret = build_pages(rva, layout->page_count << SE_PAGE_SHIFT, source, sinfo, layout->attributes)))
        {
            return ret;
        }
    }
    return SGX_SUCCESS;
}
int CLoader::build_contexts(layout_t *layout_start, layout_t *layout_end, uint64_t delta)
{
    int ret = SGX_ERROR_UNEXPECTED;
    for(layout_t *layout = layout_start; layout < layout_end; layout++)
    {
        if (!IS_GROUP_ID(layout->group.id))
        {
            if(SGX_SUCCESS != (ret = build_context(delta, &layout->entry)))
            {
                return ret;
            }
        }
        else
        {
            uint64_t step = 0;
            for(uint32_t j = 0; j < layout->group.load_times; j++)
            {
                step += layout->group.load_step;
                if(SGX_SUCCESS != (ret = build_contexts(&layout[-layout->group.entry_count], layout, step)))
                {
                    return ret;
                }
            }
        }
    }
    return SGX_SUCCESS;
}
int CLoader::build_secs(sgx_attributes_t * const secs_attr, sgx_misc_attribute_t * const misc_attr)
{
    memset(&m_secs, 0, sizeof(secs_t)); //should set resvered field of secs as 0.
    //create secs structure.
    m_secs.base = 0;    //base is allocated by driver. set it as 0
    m_secs.size = m_metadata->enclave_size;
    m_secs.misc_select = misc_attr->misc_select;

    memcpy_s(&m_secs.attributes,  sizeof(m_secs.attributes), secs_attr, sizeof(m_secs.attributes));
    m_secs.ssa_frame_size = m_metadata->ssa_frame_size;

    EnclaveCreator *enclave_creator = get_enclave_creator();
    if(NULL == enclave_creator)
        return SGX_ERROR_UNEXPECTED;
    int ret = enclave_creator->create_enclave(&m_secs, &m_enclave_id, &m_start_addr, is_ae(&m_metadata->enclave_css));
    if(SGX_SUCCESS == ret)
    {
        SE_TRACE(SE_TRACE_NOTICE, "enclave start address = %p, size = %x\n", m_start_addr, m_metadata->enclave_size);
    }
    return ret;
}
int CLoader::build_image(SGXLaunchToken * const lc, sgx_attributes_t * const secs_attr, le_prd_css_file_t *prd_css_file, sgx_misc_attribute_t * const misc_attr)
{
    int ret = SGX_SUCCESS;

    if(SGX_SUCCESS != (ret = build_secs(secs_attr, misc_attr)))
    {
        SE_TRACE(SE_TRACE_WARNING, "build secs failed\n");
        return ret;
    };
    // read reloc bitmap before patch the enclave file
    // If load_enclave_ex try to load the enclave for the 2nd time,
    // the enclave image is already patched, and parser cannot read the information.
    // For linux, there's no map conflict. We assume load_enclave_ex will not do the retry.
    vector<uint8_t> bitmap;
    if(!m_parser.get_reloc_bitmap(bitmap))
        return SGX_ERROR_INVALID_ENCLAVE;

    // patch enclave file
    patch_entry_t *patch_start = GET_PTR(patch_entry_t, m_metadata, m_metadata->dirs[DIR_PATCH].offset);
    patch_entry_t *patch_end = GET_PTR(patch_entry_t, m_metadata, m_metadata->dirs[DIR_PATCH].offset + m_metadata->dirs[DIR_PATCH].size);
    for(patch_entry_t *patch = patch_start; patch < patch_end; patch++)
    {
        memcpy_s(GET_PTR(void, m_parser.get_start_addr(), patch->dst), patch->size, GET_PTR(void, m_metadata, patch->src), patch->size);
    }

    //build sections, copy export function table as well;
    if(SGX_SUCCESS != (ret = build_sections(&bitmap)))
    {
        SE_TRACE(SE_TRACE_WARNING, "build sections failed\n");
        goto fail;
    }

    // build heap/thread context
    if (SGX_SUCCESS != (ret = build_contexts(GET_PTR(layout_t, m_metadata, m_metadata->dirs[DIR_LAYOUT].offset), 
                                      GET_PTR(layout_t, m_metadata, m_metadata->dirs[DIR_LAYOUT].offset + m_metadata->dirs[DIR_LAYOUT].size),
                                      0)))
    {
        SE_TRACE(SE_TRACE_WARNING, "build heap/thread context failed\n");
        goto fail;
    }

    //initialize Enclave
    ret = get_enclave_creator()->init_enclave(ENCLAVE_ID_IOCTL, const_cast<enclave_css_t *>(&m_metadata->enclave_css), lc, prd_css_file);
    if(SGX_SUCCESS != ret)
    {
        SE_TRACE(SE_TRACE_WARNING, "init_enclave failed\n");
        goto fail;
    }

    return SGX_SUCCESS;

fail:
    get_enclave_creator()->destroy_enclave(ENCLAVE_ID_IOCTL);

    return ret;
}
bool CLoader::is_metadata_buffer(uint32_t offset, uint32_t size)
{
    if((offsetof(metadata_t, data) > offset) || (offset >= m_metadata->size))
    {
        return false;
    }
    uint32_t end = offset + size;
    if ((end < offset) || (end < size) || (end > m_metadata->size))
    {
        return false;
    }
    return true;
}
bool CLoader::is_enclave_buffer(uint64_t offset, uint64_t size)
{
    if(offset >= m_metadata->enclave_size)
    {
        return false;
    }
    uint64_t end = offset + size;
    if ((end < offset) || (end < size) || (end > m_metadata->enclave_size))
    {
        return false;
    }
    return true;
}
int CLoader::validate_layout_table()
{
    layout_t *layout_start = GET_PTR(layout_t, m_metadata, m_metadata->dirs[DIR_LAYOUT].offset);
    layout_t *layout_end = GET_PTR(layout_t, m_metadata, m_metadata->dirs[DIR_LAYOUT].offset + m_metadata->dirs[DIR_LAYOUT].size);
    vector<pair<uint64_t, uint64_t>> rva_vector;
    for (layout_t *layout = layout_start; layout < layout_end; layout++)
    {
        if(!IS_GROUP_ID(layout->entry.id))  // layout entry
        {
            rva_vector.push_back(make_pair(layout->entry.rva, layout->entry.page_count << SE_PAGE_SHIFT));
            if(layout->entry.content_offset)
            {
                if(false == is_metadata_buffer(layout->entry.content_offset, layout->entry.content_size))
                {
                    return SGX_ERROR_INVALID_METADATA;
                }
            }
        }
        else // layout group
        {
            if (layout->group.entry_count > (uint32_t)(PTR_DIFF(layout, layout_start)/sizeof(layout_t)))
            {
                return SGX_ERROR_INVALID_METADATA;
            }
            uint64_t load_step = 0;
            for(uint32_t i = 0; i < layout->group.load_times; i++)
            {
                load_step += layout->group.load_step;
                if(load_step > m_metadata->enclave_size)
                {
                    return SGX_ERROR_INVALID_METADATA;
                }
                for(layout_entry_t *entry = &layout[-layout->group.entry_count].entry; entry < &layout->entry; entry++)
                {
                    if(IS_GROUP_ID(entry->id))
                    {
                        return SGX_ERROR_INVALID_METADATA;
                    }
                    rva_vector.push_back(make_pair(entry->rva + load_step, entry->page_count << SE_PAGE_SHIFT));
                    // no need to check integer overflow for entry->rva + load_step, because
                    // entry->rva and load_step are less than enclave_size, whose size is no more than 37 bit
                }
            }
        }
    }
    sort(rva_vector.begin(), rva_vector.end());
    for (vector<pair<uint64_t, uint64_t>>::iterator it = rva_vector.begin(); it != rva_vector.end(); it++)
    {
        if(!IS_PAGE_ALIGNED(it->first))
        {
            return SGX_ERROR_INVALID_METADATA;
        }
        if(false == is_enclave_buffer(it->first, it->second))
        {
            return SGX_ERROR_INVALID_METADATA;
        }
        if((it+1) != rva_vector.end())
        {
            if((it->first+it->second) > (it+1)->first)
            {
                return SGX_ERROR_INVALID_METADATA;
            }
        }
    }
    return SGX_SUCCESS;
}

int CLoader::validate_patch_table()
{
    patch_entry_t *patch_start = GET_PTR(patch_entry_t, m_metadata, m_metadata->dirs[DIR_PATCH].offset);
    patch_entry_t *patch_end = GET_PTR(patch_entry_t, m_metadata, m_metadata->dirs[DIR_PATCH].offset + m_metadata->dirs[DIR_PATCH].size);
    for(patch_entry_t *patch = patch_start; patch < patch_end; patch++)
    {
        if(false == is_metadata_buffer(patch->src, patch->size))
        {
            return SGX_ERROR_INVALID_METADATA;
        }
        if(false == is_enclave_buffer(patch->dst, patch->size))
        {
            return SGX_ERROR_INVALID_METADATA;
        }
    }
    return SGX_SUCCESS;
}
int CLoader::validate_metadata()
{
    if(!m_metadata)
        return SGX_ERROR_INVALID_METADATA;
    uint64_t version = META_DATA_MAKE_VERSION(MAJOR_VERSION,MINOR_VERSION );
    //if the version of metadata does NOT match the version of metadata in urts, we should NOT launch enclave.
    if(m_metadata->version != version)
    {
        SE_TRACE(SE_TRACE_WARNING, "Mismatch between the metadata urts required and the metadata in use.\n");
        return SGX_ERROR_INVALID_VERSION;
    }
    if(m_metadata->size > sizeof(metadata_t))
    {
        return SGX_ERROR_INVALID_METADATA;
    }
    if(m_metadata->tcs_policy > TCS_POLICY_UNBIND)
        return SGX_ERROR_INVALID_METADATA;
    if(m_metadata->ssa_frame_size < SSA_FRAME_SIZE_MIN || m_metadata->ssa_frame_size > SSA_FRAME_SIZE_MAX)
        return SGX_ERROR_INVALID_METADATA;
    uint64_t size = m_metadata->enclave_size;
    if(size > m_parser.get_enclave_max_size())
    {
        return SGX_ERROR_INVALID_METADATA;
    }
    while ((size != 0) && ((size & 1) != 1))
    {
        size = size >> 1;
    }
    if(size != 1)
    {
        return SGX_ERROR_INVALID_METADATA;
    }

    // check dirs
    for(uint32_t i = 0; i < DIR_NUM; i++)
    {
        if(false == is_metadata_buffer(m_metadata->dirs[i].offset, m_metadata->dirs[i].size))
        {
            return SGX_ERROR_INVALID_METADATA;
        }
    }
    // check layout table
    int status = validate_layout_table();
    if(SGX_SUCCESS != status)
    {
        return status;
    }
    // check patch table
    status = validate_patch_table();
    if(SGX_SUCCESS != status)
    {
        return status;
    }

    return SGX_SUCCESS;
}

bool CLoader::is_ae(const enclave_css_t *enclave_css)
{
    assert(NULL != enclave_css);

    if(INTEL_VENDOR_ID == enclave_css->header.module_vendor
            && AE_PRODUCT_ID == enclave_css->body.isv_prod_id)
        return true;

    return false;
}

int CLoader::load_enclave(SGXLaunchToken *lc, int debug, const metadata_t *metadata, le_prd_css_file_t *prd_css_file, sgx_misc_attribute_t *misc_attr)
{
    int ret = SGX_SUCCESS;
    sgx_misc_attribute_t sgx_misc_attr;
    memset(&sgx_misc_attr, 0, sizeof(sgx_misc_attribute_t));

    m_metadata = metadata;
    ret = validate_metadata();
    if(SGX_SUCCESS != ret)
    {
        SE_TRACE(SE_TRACE_ERROR, "The metadata setting is not correct\n");
        return ret;
    }

    ret = get_enclave_creator()->get_misc_attr(&sgx_misc_attr, const_cast<metadata_t *>(m_metadata), lc, debug);
    if(SGX_SUCCESS != ret)
    {
        return ret;
    }

    ret = build_image(lc, &sgx_misc_attr.secs_attr, prd_css_file, &sgx_misc_attr);
    //return platform capability if fail. Otherwise, return secs.attr.
    if(SGX_SUCCESS == ret)
    {
        if(misc_attr)
        {
            memcpy_s(misc_attr, sizeof(sgx_misc_attribute_t), &sgx_misc_attr, sizeof(sgx_misc_attribute_t));
            //When run here EINIT success, so SGX_FLAGS_INITTED should be set by ucode. uRTS align it with EINIT instruction.
            misc_attr->secs_attr.flags |= SGX_FLAGS_INITTED;
        }
    }
    else
    {
        if(misc_attr)
        {
            sgx_misc_attribute_t plat_cap;
            memset(&plat_cap, 0, sizeof(plat_cap));
            get_enclave_creator()->get_plat_cap(&plat_cap);
            memcpy_s(misc_attr, sizeof(sgx_misc_attribute_t), &plat_cap, sizeof(sgx_misc_attribute_t));
        }
    }

    return ret;
}

int CLoader::load_enclave_ex(SGXLaunchToken *lc, bool debug, const metadata_t *metadata, le_prd_css_file_t *prd_css_file, sgx_misc_attribute_t *misc_attr)
{
    unsigned int ret = SGX_SUCCESS, map_conflict_count = 3;
    bool retry = true;

    while (retry)
    {
        ret = this->load_enclave(lc, debug, metadata, prd_css_file, misc_attr);
        switch(ret)
        {
            //If CreateEnclave failed due to power transition, we retry it.
        case SGX_ERROR_ENCLAVE_LOST:     //caused by loading enclave while power transition occurs
            break;

            //If memroy map conflict occurs, we only retry 3 times.
        case SGX_ERROR_MEMORY_MAP_CONFLICT:
            if(0 == map_conflict_count)
                retry = false;
            else
                map_conflict_count--;
            break;

            //We don't re-load enclave due to other error code.
        default:
            retry = false;
            break;
        }
    }

    return ret;
}

int CLoader::destroy_enclave()
{
    return get_enclave_creator()->destroy_enclave(ENCLAVE_ID_IOCTL);
}

int CLoader::set_memory_protection()
{

    uint64_t rva = 0;
    uint64_t len = 0;
    uint64_t last_section_end = 0;
    unsigned int i = 0;
    int ret = 0;
    //for sections
    std::vector<Section*> sections = m_parser.get_sections();

    for(i = 0; i < sections.size() ; i++)
    {
        //require the sec_info.rva be page aligned, we need handle the first page.
        //the first page;
        uint64_t offset = (sections[i]->get_rva() & (SE_PAGE_SIZE -1));
        uint64_t size = SE_PAGE_SIZE - offset;

        //the raw data may be smaller than the size, we get the min of them
        if(sections[i]->raw_data_size() < size)
            size = sections[i]->raw_data_size();

        len = SE_PAGE_SIZE;

        //if there is more pages, then calc the next paged aligned pages
        if((sections[i]->virtual_size() + offset) >  SE_PAGE_SIZE)
        {
            uint64_t raw_data_size = sections[i]->raw_data_size() - size;
            //we need use (SE_PAGE_SIZE - offset), because (SE_PAGE_SIZE - offset) may larger than size
            uint64_t virtual_size = sections[i]->virtual_size() - (SE_PAGE_SIZE - offset);
            len += ROUND_TO_PAGE(raw_data_size);

            if(ROUND_TO_PAGE(virtual_size) > ROUND_TO_PAGE(raw_data_size))
            {
                len += ROUND_TO_PAGE(virtual_size) - ROUND_TO_PAGE(raw_data_size);
            }
        }
        rva = TRIM_TO_PAGE(sections[i]->get_rva()) + (uint64_t)m_start_addr;
        ret = mprotect((void*)rva, (size_t)len, (int)(sections[i]->get_si_flags()&SI_MASK_MEM_ATTRIBUTE));
        if(ret != 0)
        {
            SE_TRACE(SE_TRACE_WARNING, "section[%d]:mprotect(rva=%" PRIu64 ", len=%" PRIu64 ", flags=%" PRIu64 ") failed\n",
                     i, rva, len, (sections[i]->get_si_flags()));
            return SGX_ERROR_UNEXPECTED;
        }
        //there is a gap between sections, need to set those to NONE access
        if(last_section_end != 0)
        {
            ret = mprotect((void*)last_section_end, (size_t)(rva - last_section_end), (int)(SI_FLAG_NONE & SI_MASK_MEM_ATTRIBUTE));
            if(ret != 0)
            {
                SE_TRACE(SE_TRACE_WARNING, "set protection for gap before section[%d]:mprotect(rva=%" PRIu64 ", len=%" PRIu64 ", flags=%" PRIu64 ") failed\n",
                         i, last_section_end, rva - last_section_end, SI_FLAG_NONE);
                return SGX_ERROR_UNEXPECTED;
            }
        }
        last_section_end = rva + len;
    }

    ret = set_context_protection(GET_PTR(layout_t, m_metadata, m_metadata->dirs[DIR_LAYOUT].offset), 
                                    GET_PTR(layout_t, m_metadata, m_metadata->dirs[DIR_LAYOUT].offset + m_metadata->dirs[DIR_LAYOUT].size), 
                                    0);
    if (SGX_SUCCESS != ret)
    {
        return ret;
    } 
    
    return SGX_SUCCESS;

}

int CLoader::set_context_protection(layout_t *layout_start, layout_t *layout_end, uint64_t delta)
{
    int ret = SGX_ERROR_UNEXPECTED;
    for(layout_t *layout = layout_start; layout < layout_end; layout++)
    {
        if (!IS_GROUP_ID(layout->group.id))
        {
            int prot = 0 ;
            if(layout->entry.attributes == SI_FLAG_NONE)
            {
                prot = SI_FLAG_NONE & SI_MASK_MEM_ATTRIBUTE;
            }
            else
            {
                prot = SI_FLAGS_RW & SI_MASK_MEM_ATTRIBUTE;
            }
            ret = mprotect(GET_PTR(void, m_start_addr, layout->entry.rva + delta), 
                               (size_t)(layout->entry.page_count << SE_PAGE_SHIFT),
                               prot); 
            if(ret != 0)
            {
                SE_TRACE(SE_TRACE_WARNING, "mprotect(rva=%" PRIu64 ", len=%" PRIu64 ", flags=%d) failed\n",
                         (uint64_t)m_start_addr + layout->entry.rva + delta, 
                         (uint64_t)(layout->entry.page_count << SE_PAGE_SHIFT), 
                          prot);
                return SGX_ERROR_UNEXPECTED;
            }
        }
        else
        {
            uint64_t step = 0;
            for(uint32_t j = 0; j < layout->group.load_times; j++)
            {
                step += layout->group.load_step;
                if(SGX_SUCCESS != (ret = set_context_protection(&layout[-layout->group.entry_count], layout, step)))
                {
                    return ret;
                }
            }
        }
    }
    return SGX_SUCCESS;
}
