/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "NPL"); you may not use this file except in
 * compliance with the NPL.  You may obtain a copy of the NPL at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the NPL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the NPL
 * for the specific language governing rights and limitations under the
 * NPL.
 *
 * The Initial Developer of this code under the NPL is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation.  All Rights
 * Reserved.
 */

#include "nsIString.h"
#include "nsIStreamListener.h"
#include "nsHTTPResponseListener.h"
#include "nsIChannel.h"
#include "nsIInputStream.h"
#include "nsHTTPChannel.h"
#include "nsHTTPResponse.h"
#include "nsIHttpEventSink.h"
#include "nsCRT.h"
#include "stdio.h" //sscanf

static const int kMAX_FIRST_LINE_SIZE= 256;

nsHTTPResponseListener::nsHTTPResponseListener(): 
    m_pConnection(nsnull),
    m_bFirstLineParsed(PR_FALSE),
    m_pResponse(nsnull),
    m_bHeadersDone(PR_FALSE)
{
    NS_INIT_REFCNT();
}

nsHTTPResponseListener::~nsHTTPResponseListener()
{
    if (m_pConnection)
        NS_RELEASE(m_pConnection);
    if (m_pResponse)
        NS_RELEASE(m_pResponse);
}

NS_IMPL_ISUPPORTS(nsHTTPResponseListener,nsIStreamListener::GetIID());

NS_IMETHODIMP
nsHTTPResponseListener::OnDataAvailable(nsISupports* context,
                            nsIInputStream *i_pStream, 
                            PRUint32 i_SourceOffset,
                            PRUint32 i_Length)
{
    nsIInputStream* inStr = nsnull;

    // Should I save this as a member variable? yes... todo
    nsIHTTPEventSink* pSink= nsnull;
    m_pConnection->GetEventSink(&pSink);
    NS_VERIFY(pSink, "No HTTP Event Sink!");

    NS_ASSERTION(i_pStream, "Fake stream!");
    // Set up the response
    if (!m_pResponse)
    {
 
        // why do I need the connection in the constructor... get rid.. TODO
        m_pResponse = new nsHTTPResponse (m_pConnection, /* inStr */ i_pStream);
        if (!m_pResponse)
        {
            NS_ERROR("Failed to create the response object!");
            return NS_ERROR_OUT_OF_MEMORY;
        }
        NS_ADDREF(m_pResponse);
        nsHTTPChannel* pTestCon = NS_STATIC_CAST(nsHTTPChannel*, m_pConnection);
        pTestCon->SetResponse(m_pResponse);
    }
 
    //printf("nsHTTPResponseListener::OnDataAvailable...\n");

    char extrabuffer[kMAX_FIRST_LINE_SIZE];
    int extrabufferlen = 0;
    
    char partHeader[kMAX_FIRST_LINE_SIZE];
    int partHeaderLen = 0;

    while (!m_bHeadersDone)
    {
        //TODO optimize this further!
        char buffer[kMAX_FIRST_LINE_SIZE];  
        PRUint32 length;

        nsresult stat = i_pStream->Read(buffer, kMAX_FIRST_LINE_SIZE, &length);
        NS_ASSERTION(buffer, "Argh...");

        char* p = buffer;
        while (buffer+length > p)
        {
            char* lineStart = p;
            if (*lineStart == '\0' || *lineStart == CR)
            {
                m_bHeadersDone = PR_TRUE;
                // we read extra so save it for the other headers
                if (buffer+length > p)
                {
                    extrabufferlen = length - (buffer - p);
                    PL_strncpy(extrabuffer, p, extrabufferlen);
                }
                
                //TODO process headers here. 

                pSink->OnHeadersAvailable(context);

                break; // break off this buffer while
            }
            while ((*p != LF) && (buffer+length > p))
                ++p;
            if (!m_bFirstLineParsed)
            {
                char server_version[8]; // HTTP/1.1 
                PRUint32 stat = 0;
                char stat_str[kMAX_FIRST_LINE_SIZE];
                *p = '\0';
                sscanf(lineStart, "%8s %d %s", server_version, &stat, stat_str);
                m_pResponse->SetServerVersion(server_version);
                m_pResponse->SetStatus(stat);
                m_pResponse->SetStatusString(stat_str);
                p++;
                m_bFirstLineParsed = PR_TRUE;
            }
            else
            {
                char* header = lineStart;
                char* value = PL_strchr(lineStart, ':');
                *p = '\0';
                if(value)
                {
                    *value = '\0';
                    value++;
                    if (partHeaderLen == 0)
                        m_pResponse->SetHeaderInternal(header, value);
                    else
                    {
                        //append the header to the partheader
                        header = PL_strcat(partHeader, header);
                        m_pResponse->SetHeaderInternal(header, value);
                        //Reset partHeader now
                        partHeader[0]='\0';
                        partHeaderLen = 0;
                    }

                }
                else // this is just a part of the header so save it for later use...
                {
                    partHeaderLen = p-header;
                    PL_strncpy(partHeader, lineStart, partHeaderLen);
                }
                p++;
            }
        
        }
    }

    if (m_bHeadersDone)
    {
        nsresult rv;
        nsIStreamListener* internalListener = nsnull;

        // Get our end of the pipe between us and the user's GetInputStream()
        rv = m_pConnection->GetResponseDataListener(&internalListener);
        if (internalListener) {
            // post the data to the stream listener
            // XXX this is the wrong data and offsets I think.
            rv = internalListener->OnDataAvailable(context, i_pStream, i_SourceOffset, i_Length);
            NS_RELEASE(internalListener);
            if (NS_FAILED(rv))
                return rv;
        }

        // do whatever we want for the event sink
        return pSink->OnDataAvailable(context, /* inStr */i_pStream, i_SourceOffset, i_Length);
    }

    return NS_OK;
}

NS_IMETHODIMP
nsHTTPResponseListener::OnStartBinding(nsISupports* i_pContext)
{
    nsresult rv;
    nsIHTTPEventSink* pSink= nsnull;

    //TODO globally replace printf with trace calls. 
    //printf("nsHTTPResponseListener::OnStartBinding...\n");

    // Initialize header varaibles...  
    m_bHeadersDone     = PR_FALSE;
    m_bFirstLineParsed = PR_FALSE;

    if (i_pContext) {
        rv = i_pContext->QueryInterface(nsIHTTPChannel::GetIID(), 
                                        (void**)&m_pConnection);
    } else {
        rv = NS_ERROR_NULL_POINTER;
    }

    if (NS_SUCCEEDED(rv)) {
        // XXX:  EventSink(...) should AddRef the returned pointer...
        rv = m_pConnection->GetEventSink(&pSink);

        if (NS_FAILED(rv)) {
            NS_ERROR("No HTTP Event Sink!");
        }
    }

    if (NS_SUCCEEDED(rv)) {
        rv = pSink->OnStartBinding(i_pContext);
    }

    return rv;
}

NS_IMETHODIMP
nsHTTPResponseListener::OnStopBinding(nsISupports* i_pContext,
                                 nsresult i_Status,
                                 nsIString* i_pMsg)
{
    //printf("nsHTTPResponseListener::OnStopBinding...\n");
    //NS_ASSERTION(m_pResponse, "Response object not created yet or died?!");
    // Should I save this as a member variable? yes... todo
    nsIHTTPEventSink* pSink= nsnull;
    nsresult rv = m_pConnection->GetEventSink(&pSink);
    if (NS_FAILED(rv))
        NS_ERROR("No HTTP Event Sink!");
    
    rv = pSink->OnStopBinding(i_pContext, i_Status,i_pMsg);

    return rv;
}

NS_IMETHODIMP
nsHTTPResponseListener::OnStartRequest(nsISupports* i_pContext)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsHTTPResponseListener::OnStopRequest(nsISupports* i_pContext,
                                      nsresult iStatus,
                                      nsIString* i_pMsg)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

